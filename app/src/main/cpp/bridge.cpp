#include <jni.h>
#include <dwg.h>
#include <dwg_api.h>
extern "C" {
#include <codepages.h>
}

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;

struct Point { double x, y; };
struct Transform {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Point apply(Point p) const { return {a * p.x + c * p.y + tx, b * p.x + d * p.y + ty}; }
    Transform then(const Transform &other) const {
        return {a * other.a + c * other.b, b * other.a + d * other.b,
                a * other.c + c * other.d, b * other.c + d * other.d,
                a * other.tx + c * other.ty + tx, b * other.tx + d * other.ty + ty};
    }
};
struct Clip { bool active = false; double left = 0, bottom = 0, right = 0, top = 0; };

class Json {
public:
    std::string value;
    void raw(const char *text) { value += text; }
    void raw(const std::string &text) { value += text; }
    void number(double n) {
        if (!std::isfinite(n)) n = 0;
        char text[64];
        std::snprintf(text, sizeof(text), "%.12g", n);
        value += text;
    }
    void quote(const std::string &text) {
        value += '"';
        for (unsigned char ch : text) {
            switch (ch) {
                case '"': value += "\\\""; break;
                case '\\': value += "\\\\"; break;
                case '\n': value += "\\n"; break;
                case '\r': value += "\\r"; break;
                case '\t': value += "\\t"; break;
                default:
                    if (ch < 0x20) {
                        char encoded[7]; std::snprintf(encoded, sizeof(encoded), "\\u%04x", ch);
                        value += encoded;
                    } else value += static_cast<char>(ch);
            }
        }
        value += '"';
    }
};

void appendUtf8(std::string &target, unsigned code) {
    if (code < 0x80) target += static_cast<char>(code);
    else if (code < 0x800) {
        target += static_cast<char>(0xc0 | (code >> 6));
        target += static_cast<char>(0x80 | (code & 0x3f));
    } else {
        target += static_cast<char>(0xe0 | (code >> 12));
        target += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
        target += static_cast<char>(0x80 | (code & 0x3f));
    }
}

std::string decodeText(const char *raw, Dwg_Data *dwg) {
    if (!raw) return {};
    if (dwg->header.version >= R_2007 || dwg->header.codepage == CP_UTF8) return raw;
    auto codepage = static_cast<Dwg_Codepage>(dwg->header.codepage);
    std::string result;
    const auto *bytes = reinterpret_cast<const unsigned char *>(raw);
    while (*bytes) {
        unsigned value = *bytes++;
        if (value >= 0x80 && dwg_codepage_is_twobyte(codepage, value) && *bytes)
            value = (value << 8) | *bytes++;
        unsigned unicode = static_cast<unsigned>(dwg_codepage_uwc(codepage, value));
        appendUtf8(result, unicode ? unicode : 0xfffd);
    }
    return result;
}

std::string utf8(void *item, const char *type, const char *field, Dwg_Data *dwg) {
    char *text = nullptr;
    int allocated = 0;
    if (!dwg_dynapi_entity_utf8text(item, type, field, &text, &allocated, nullptr)) return {};
    std::string result = decodeText(text, dwg);
    if (allocated) std::free(text);
    return result;
}

std::string nameOfLayer(Dwg_Object_LAYER *layer, Dwg_Data *dwg) {
    if (!layer) return "0";
    int error = 0;
    char *name = dwg_obj_layer_get_name(layer, &error);
    std::string result = (!error && name) ? decodeText(name, dwg) : "0";
    if (name && dwg->header.version >= R_2007) std::free(name);
    return result;
}

int indexedColor(int index) {
    if (index < 1 || index > 255) return 0xffffff;
    return static_cast<int>(dwg_rgb_palette_index(index) & 0xffffff);
}

struct Layer { std::string name; bool visible; int color; };

class Reader {
public:
    explicit Reader(Dwg_Data *drawing) : dwg(drawing) {
        Dwg_Object_LAYER **items = dwg_getall_LAYER(dwg);
        if (items) {
            for (size_t i = 0; items[i]; ++i) {
                Dwg_Object_LAYER *item = items[i];
                Layer layer{nameOfLayer(item, dwg), !item->off && !item->frozen,
                            colorOf(item->color, 0xffffff)};
                layers[layer.name] = layer;
            }
            std::free(items);
        }
        if (layers.empty()) layers["0"] = {"0", true, 0xffffff};
    }

    std::string convert() {
        out.raw("{\"layers\":[");
        bool first = true;
        for (const auto &entry : layers) {
            if (!first) out.raw(","); first = false;
            out.raw("{\"name\":"); out.quote(entry.second.name);
            out.raw(",\"visible\":"); out.raw(entry.second.visible ? "true" : "false");
            out.raw(",\"color\":"); out.number(entry.second.color); out.raw("}");
        }
        out.raw("],\"spaces\":[");
        Dwg_Object_Ref *modelRef = dwg_model_space_ref(dwg);
        int error = 0;
        Dwg_Object *model = modelRef ? dwg_ref_get_object(modelRef, &error) : nullptr;
        if (!model) throw std::runtime_error("图纸缺少模型空间");
        writeSpace("模型", model, model, false);

        Dwg_Object_LAYOUT **layouts = dwg_getall_LAYOUT(dwg);
        if (layouts) {
            std::vector<Dwg_Object_LAYOUT *> papers;
            for (size_t i = 0; layouts[i]; ++i) {
                if (!layouts[i]->block_header) continue;
                Dwg_Object *block = dwg_ref_get_object(layouts[i]->block_header, &error);
                if (block && block != model) papers.push_back(layouts[i]);
            }
            std::sort(papers.begin(), papers.end(), [](const auto *a, const auto *b) {
                return a->tab_order < b->tab_order;
            });
            for (auto *layout : papers) {
                Dwg_Object *block = dwg_ref_get_object(layout->block_header, &error);
                std::string name = utf8(layout, "LAYOUT", "layout_name", dwg);
                out.raw(",");
                writeSpace(name.empty() ? "布局" : name, block, model, true);
            }
            std::free(layouts);
        }
        out.raw("]}");
        return std::move(out.value);
    }

private:
    Dwg_Data *dwg;
    Json out;
    std::map<std::string, Layer> layers;
    bool firstEntity = true;

    int colorOf(const Dwg_Color &color, int fallback) const {
        if (color.method == DWG_COLOR_METHOD_TRUECOLOR) return static_cast<int>(color.rgb & 0xffffff);
        if (color.index >= 1 && color.index <= 255) return indexedColor(color.index);
        return fallback;
    }

    std::string entityLayer(Dwg_Object *object, const std::string &inherited) const {
        if (!object || !object->tio.entity || !object->tio.entity->layer) return inherited;
        int error = 0;
        Dwg_Object *layerObj = dwg_ref_get_object(object->tio.entity->layer, &error);
        auto *layer = layerObj ? dwg_object_to_LAYER(layerObj) : nullptr;
        std::string name = nameOfLayer(layer, dwg);
        return name == "0" && !inherited.empty() ? inherited : name;
    }

    int entityColor(Dwg_Object *object, const std::string &layer, int inherited) const {
        int layerColor = layers.count(layer) ? layers.at(layer).color : 0xffffff;
        if (!object || !object->tio.entity) return layerColor;
        const auto &color = object->tio.entity->color;
        if (color.index == 0 || color.method == DWG_COLOR_METHOD_BYBLOCK) return inherited;
        return colorOf(color, layerColor);
    }

    void beginEntity(Dwg_Object *object, const char *kind, const std::string &layer,
                     int color, const Clip &clip, Dwg_Object *owner) {
        if (!firstEntity) out.raw(","); firstEntity = false;
        out.raw("{\"kind\":"); out.quote(kind);
        out.raw(",\"type\":"); out.quote(owner && owner->name ? owner->name : object->name ? object->name : "ENTITY");
        char handle[32];
        std::snprintf(handle, sizeof(handle), "%llX",
                      static_cast<unsigned long long>((owner ? owner : object)->handle.value));
        out.raw(",\"handle\":"); out.quote(handle);
        out.raw(",\"layer\":"); out.quote(layer);
        if (owner) {
            out.raw(",\"ownerLayer\":"); out.quote(entityLayer(owner, ""));
        }
        out.raw(",\"color\":"); out.number(color);
        if (clip.active) {
            out.raw(",\"clip\":["); out.number(clip.left); out.raw(","); out.number(clip.bottom);
            out.raw(","); out.number(clip.right); out.raw(","); out.number(clip.top); out.raw("]");
        }
    }

    void writePoints(const std::vector<Point> &points, const Transform &transform) {
        out.raw("[");
        bool first = true;
        for (const auto &point : points) {
            Point p = transform.apply(point);
            if (!first) out.raw(","); first = false;
            out.number(p.x); out.raw(","); out.number(p.y);
        }
        out.raw("]");
    }

    void path(Dwg_Object *object, const std::vector<Point> &points, bool closed, bool fill,
              const Transform &transform, const Clip &clip, const std::string &layer,
              int color, Dwg_Object *owner) {
        if (points.size() < 2) return;
        beginEntity(object, "path", layer, color, clip, owner);
        out.raw(",\"points\":"); writePoints(points, transform);
        out.raw(",\"closed\":"); out.raw(closed ? "true" : "false");
        out.raw(",\"fill\":"); out.raw(fill ? "true" : "false");
        out.raw("}");
    }

    static void appendBulge(std::vector<Point> &points, Point start, Point end, double bulge) {
        if (std::abs(bulge) < 1e-9) { points.push_back(end); return; }
        double dx = end.x - start.x, dy = end.y - start.y;
        double length = std::hypot(dx, dy);
        if (length < 1e-9) return;
        double h = length * (1 - bulge * bulge) / (4 * bulge);
        double cx = (start.x + end.x) / 2 - dy / length * h;
        double cy = (start.y + end.y) / 2 + dx / length * h;
        double radius = std::hypot(start.x - cx, start.y - cy);
        double begin = std::atan2(start.y - cy, start.x - cx);
        double sweep = 4 * std::atan(bulge);
        int count = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 24))));
        for (int i = 1; i <= count; ++i) {
            double angle = begin + sweep * i / count;
            points.push_back({cx + radius * std::cos(angle), cy + radius * std::sin(angle)});
        }
    }

    static std::vector<Point> arc(Point center, double radius, double start, double end) {
        if (radius <= 0 || !std::isfinite(radius)) return {};
        while (end <= start) end += 2 * kPi;
        int count = std::max(12, static_cast<int>(std::ceil((end - start) / (kPi / 32))));
        count = std::min(count, 256);
        std::vector<Point> points;
        points.reserve(count + 1);
        for (int i = 0; i <= count; ++i) {
            double angle = start + (end - start) * i / count;
            points.push_back({center.x + radius * std::cos(angle),
                              center.y + radius * std::sin(angle)});
        }
        return points;
    }

    void text(Dwg_Object *object, const std::string &content, Point at, double height,
              double rotation, const Transform &transform, const Clip &clip,
              const std::string &layer, int color, Dwg_Object *owner) {
        if (content.empty() || height <= 0) return;
        Point p = transform.apply(at);
        Point axis = transform.apply({at.x + std::cos(rotation), at.y + std::sin(rotation)});
        double angle = std::atan2(axis.y - p.y, axis.x - p.x);
        double size = height * std::hypot(transform.c, transform.d);
        beginEntity(object, "text", layer, color, clip, owner);
        out.raw(",\"text\":"); out.quote(content);
        out.raw(",\"x\":"); out.number(p.x);
        out.raw(",\"y\":"); out.number(p.y);
        out.raw(",\"height\":"); out.number(size);
        out.raw(",\"rotation\":"); out.number(angle);
        out.raw("}");
    }

    static std::string plainMtext(const std::string &source) {
        std::string result;
        for (size_t i = 0; i < source.size(); ++i) {
            if (source[i] == '{' || source[i] == '}') continue;
            if (source[i] == '\\' && i + 1 < source.size()) {
                char command = source[++i];
                if (command == 'P' || command == 'p') result += '\n';
                else if (command == '~') result += ' ';
                else if (command == '\\' || command == '{' || command == '}') result += command;
                else if (command == 'L' || command == 'l' || command == 'O' || command == 'o') {}
                else if (command == 'U' && i + 5 < source.size() && source[i + 1] == '+') {
                    std::string digits = source.substr(i + 2, 4);
                    if (digits.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos) {
                        appendUtf8(result, static_cast<unsigned>(std::strtoul(digits.c_str(), nullptr, 16)));
                        i += 5;
                    }
                }
                else if (command == 'C' || command == 'F' || command == 'H' || command == 'T' ||
                         command == 'Q' || command == 'W' || command == 'A' || command == 'S') {
                    while (i + 1 < source.size() && source[i + 1] != ';') ++i;
                    if (i + 1 < source.size()) ++i;
                } else result += command;
            } else result += source[i];
        }
        return result;
    }

    void hatch(Dwg_Object *object, Dwg_Entity_HATCH *item, const Transform &transform,
               const Clip &clip, const std::string &layer, int color, Dwg_Object *owner) {
        if (!item || !item->paths) return;
        std::vector<std::vector<Point>> loops;
        for (unsigned i = 0; i < item->num_paths && i < 10000; ++i) {
            const Dwg_HATCH_Path &boundary = item->paths[i];
            std::vector<Point> points;
            if ((boundary.flag & 2) && boundary.polyline_paths) {
                for (unsigned j = 0; j < boundary.num_segs_or_paths && j < 100000; ++j) {
                    Point p{boundary.polyline_paths[j].point.x, boundary.polyline_paths[j].point.y};
                    if (points.empty()) points.push_back(p);
                    unsigned next = (j + 1) % boundary.num_segs_or_paths;
                    if (j + 1 < boundary.num_segs_or_paths || boundary.closed) {
                        Point q{boundary.polyline_paths[next].point.x, boundary.polyline_paths[next].point.y};
                        appendBulge(points, p, q, boundary.polyline_paths[j].bulge);
                    }
                }
            } else if (boundary.segs) {
                for (unsigned j = 0; j < boundary.num_segs_or_paths && j < 100000; ++j) {
                    const Dwg_HATCH_PathSeg &seg = boundary.segs[j];
                    if (seg.curve_type == 1) {
                        if (points.empty()) points.push_back({seg.first_endpoint.x, seg.first_endpoint.y});
                        points.push_back({seg.second_endpoint.x, seg.second_endpoint.y});
                    } else if (seg.curve_type == 2) {
                        auto curve = arc({seg.center.x, seg.center.y}, seg.radius,
                                         seg.start_angle * kPi / 180, seg.end_angle * kPi / 180);
                        if (!seg.is_ccw) std::reverse(curve.begin(), curve.end());
                        points.insert(points.end(), curve.begin(), curve.end());
                    } else if (seg.curve_type == 3) {
                        double start = seg.start_angle * kPi / 180;
                        double end = seg.end_angle * kPi / 180;
                        while (end <= start) end += 2 * kPi;
                        double majorX = seg.endpoint.x, majorY = seg.endpoint.y;
                        for (int n = 0; n <= 64; ++n) {
                            double a = start + (end - start) * n / 64;
                            points.push_back({seg.center.x + majorX * std::cos(a) - majorY * seg.minor_major_ratio * std::sin(a),
                                              seg.center.y + majorY * std::cos(a) + majorX * seg.minor_major_ratio * std::sin(a)});
                        }
                    } else if (seg.curve_type == 4 && seg.fitpts) {
                        for (unsigned n = 0; n < seg.num_fitpts && n < 100000; ++n)
                            points.push_back({seg.fitpts[n].x, seg.fitpts[n].y});
                    }
                }
            }
            if (points.size() >= 3) loops.push_back(std::move(points));
        }
        if (loops.empty()) return;
        beginEntity(object, "hatch", layer, color, clip, owner);
        out.raw(",\"loops\":[");
        for (size_t i = 0; i < loops.size(); ++i) {
            if (i) out.raw(",");
            writePoints(loops[i], transform);
        }
        out.raw("],\"fill\":"); out.raw(item->is_solid_fill ? "true" : "false");
        out.raw(",\"angle\":"); out.number(item->angle);
        out.raw(",\"spacing\":"); out.number(item->scale_spacing > 0 ? item->scale_spacing : 10.0);
        out.raw("}");
    }

    void block(Dwg_Object *header, const Transform &transform, const Clip &clip,
               const std::string &layer, int color, Dwg_Object *owner, int depth,
               Dwg_Object *model) {
        if (!header || depth > 12) return;
        Dwg_Object *object = get_first_owned_entity(header);
        unsigned count = 0;
        while (object && count++ < 2000000) {
            emitEntity(object, transform, clip, layer, color, owner, depth, model);
            Dwg_Object *next = get_next_owned_entity(header, object);
            if (next == object) break;
            object = next;
        }
    }

    void viewport(Dwg_Object *object, Dwg_Object *model, int depth) {
        auto *view = dwg_object_to_VIEWPORT(object);
        if (!view || view->id <= 1 || view->on_off <= 0 ||
            view->width <= 0 || view->height <= 0 || view->VIEWSIZE <= 0) return;
        double factor = view->height / view->VIEWSIZE;
        double co = std::cos(-view->VIEWTWIST), si = std::sin(-view->VIEWTWIST);
        Transform transform{factor * co, factor * si, -factor * si, factor * co,
                            view->center.x - factor * co * view->VIEWCTR.x + factor * si * view->VIEWCTR.y,
                            view->center.y - factor * si * view->VIEWCTR.x - factor * co * view->VIEWCTR.y};
        Clip clip{true, view->center.x - view->width / 2, view->center.y - view->height / 2,
                  view->center.x + view->width / 2, view->center.y + view->height / 2};
        block(model, transform, clip, "", 0xffffff, nullptr, depth + 1, model);
    }

    void emitEntity(Dwg_Object *object, const Transform &transform, const Clip &clip,
                    const std::string &inheritedLayer, int inheritedColor, Dwg_Object *owner,
                    int depth, Dwg_Object *model) {
        if (!object || object->supertype != DWG_SUPERTYPE_ENTITY) return;
        std::string layer = entityLayer(object, inheritedLayer);
        int color = entityColor(object, layer, inheritedColor);
        switch (object->fixedtype) {
            case DWG_TYPE_LINE: {
                auto *item = dwg_object_to_LINE(object);
                if (item) path(object, {{item->start.x, item->start.y}, {item->end.x, item->end.y}},
                               false, false, transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_CIRCLE: {
                auto *item = dwg_object_to_CIRCLE(object);
                if (item) path(object, arc({item->center.x, item->center.y}, item->radius, 0, 2 * kPi),
                               true, false, transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_ARC: {
                auto *item = dwg_object_to_ARC(object);
                if (item) path(object, arc({item->center.x, item->center.y}, item->radius,
                                           item->start_angle, item->end_angle),
                               false, false, transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_SOLID: {
                auto *item = dwg_object_to_SOLID(object);
                if (item) path(object, {{item->corner1.x, item->corner1.y},
                                       {item->corner2.x, item->corner2.y},
                                       {item->corner4.x, item->corner4.y},
                                       {item->corner3.x, item->corner3.y}},
                               true, true, transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_TRACE: {
                auto *item = dwg_object_to_TRACE(object);
                if (item) path(object, {{item->corner1.x, item->corner1.y},
                                       {item->corner2.x, item->corner2.y},
                                       {item->corner4.x, item->corner4.y},
                                       {item->corner3.x, item->corner3.y}},
                               true, true, transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_LWPOLYLINE: {
                auto *item = dwg_object_to_LWPOLYLINE(object);
                if (!item || !item->points || !item->num_points) break;
                bool closed = (item->flag & 512) || (item->flag & 1);
                std::vector<Point> points{{item->points[0].x, item->points[0].y}};
                for (unsigned i = 0; i < item->num_points && i < 100000; ++i) {
                    unsigned next = (i + 1) % item->num_points;
                    if (next == 0 && !closed) break;
                    Point start{item->points[i].x, item->points[i].y};
                    Point end{item->points[next].x, item->points[next].y};
                    double bulge = item->bulges && i < item->num_bulges ? item->bulges[i] : 0;
                    appendBulge(points, start, end, bulge);
                }
                path(object, points, closed, false, transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_POLYLINE_2D: {
                int error = 0;
                unsigned count = dwg_object_polyline_2d_get_numpoints(object, &error);
                if (error || !count || count > 100000) break;
                auto *vertices = dwg_object_polyline_2d_get_points(object, &error);
                auto *bulges = dwg_object_polyline_2d_get_bulges(object, &error);
                if (vertices) {
                    auto *item = dwg_object_to_POLYLINE_2D(object);
                    bool closed = item && (item->flag & 1);
                    std::vector<Point> points{{vertices[0].x, vertices[0].y}};
                    for (unsigned i = 0; i < count; ++i) {
                        unsigned next = (i + 1) % count;
                        if (next == 0 && !closed) break;
                        appendBulge(points, {vertices[i].x, vertices[i].y},
                                    {vertices[next].x, vertices[next].y}, bulges ? bulges[i] : 0);
                    }
                    path(object, points, closed, false, transform, clip, layer, color, owner);
                }
                std::free(vertices); std::free(bulges);
                break;
            }
            case DWG_TYPE_ELLIPSE: {
                auto *item = dwg_object_to_ELLIPSE(object);
                if (!item) break;
                double end = item->end_angle;
                while (end <= item->start_angle) end += 2 * kPi;
                std::vector<Point> points;
                for (int i = 0; i <= 96; ++i) {
                    double a = item->start_angle + (end - item->start_angle) * i / 96;
                    points.push_back({item->center.x + item->sm_axis.x * std::cos(a) - item->sm_axis.y * item->axis_ratio * std::sin(a),
                                      item->center.y + item->sm_axis.y * std::cos(a) + item->sm_axis.x * item->axis_ratio * std::sin(a)});
                }
                path(object, points, end - item->start_angle >= 2 * kPi - 1e-6, false,
                     transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_TEXT: {
                auto *item = dwg_object_to_TEXT(object);
                if (item) text(object, utf8(item, "TEXT", "text_value", dwg),
                               {item->ins_pt.x, item->ins_pt.y}, item->height, item->rotation,
                               transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_MTEXT: {
                auto *item = dwg_object_to_MTEXT(object);
                if (item) text(object, plainMtext(utf8(item, "MTEXT", "text", dwg)),
                               {item->ins_pt.x, item->ins_pt.y}, item->text_height,
                               std::atan2(item->x_axis_dir.y, item->x_axis_dir.x),
                               transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_ATTRIB: {
                auto *item = dwg_object_to_ATTRIB(object);
                if (item && !(item->flags & 1)) text(object, utf8(item, "ATTRIB", "text_value", dwg),
                    {item->ins_pt.x, item->ins_pt.y}, item->height, item->rotation,
                    transform, clip, layer, color, owner);
                break;
            }
            case DWG_TYPE_HATCH: hatch(object, dwg_object_to_HATCH(object), transform, clip, layer, color, owner); break;
            case DWG_TYPE_INSERT:
            case DWG_TYPE_ACIDBLOCKREFERENCE: {
                auto *item = object->fixedtype == DWG_TYPE_INSERT
                    ? dwg_object_to_INSERT(object) : dwg_object_to_ACIDBLOCKREFERENCE(object);
                if (!item || !item->block_header || depth >= 12) break;
                int error = 0;
                Dwg_Object *header = dwg_ref_get_object(item->block_header, &error);
                auto *blockData = header ? dwg_object_to_BLOCK_HEADER(header) : nullptr;
                if (!blockData || blockData->blkisxref) break;
                double sx = item->scale.x == 0 ? 1 : item->scale.x;
                double sy = item->scale.y == 0 ? 1 : item->scale.y;
                double co = std::cos(item->rotation), si = std::sin(item->rotation);
                double a = sx * co, b = sx * si, c = -sy * si, d = sy * co;
                Transform local{a, b, c, d,
                                item->ins_pt.x - a * blockData->base_pt.x - c * blockData->base_pt.y,
                                item->ins_pt.y - b * blockData->base_pt.x - d * blockData->base_pt.y};
                block(header, transform.then(local), clip, layer, color, owner ? owner : object, depth + 1, model);
                if (item->has_attribs) {
                    Dwg_Object *attribute = get_first_owned_subentity(object);
                    unsigned count = 0;
                    while (attribute && count++ < 10000) {
                        if (attribute->fixedtype == DWG_TYPE_ATTRIB)
                            emitEntity(attribute, transform, clip, layer, color,
                                       owner ? owner : object, depth + 1, model);
                        Dwg_Object *next = get_next_owned_subentity(object, attribute);
                        if (next == attribute) break;
                        attribute = next;
                    }
                }
                break;
            }
            case DWG_TYPE_DIMENSION_LINEAR:
            case DWG_TYPE_DIMENSION_ALIGNED:
            case DWG_TYPE_DIMENSION_ANG3PT:
            case DWG_TYPE_DIMENSION_ANG2LN:
            case DWG_TYPE_DIMENSION_RADIUS:
            case DWG_TYPE_DIMENSION_DIAMETER:
            case DWG_TYPE_DIMENSION_ORDINATE: {
                auto *dimension = dwg_object_to_DIMENSION(object);
                if (dimension && dimension->block && depth < 12) {
                    int error = 0;
                    Dwg_Object *header = dwg_ref_get_object(dimension->block, &error);
                    block(header, transform, clip, layer, color, owner ? owner : object, depth + 1, model);
                }
                break;
            }
            case DWG_TYPE_VIEWPORT: if (model && !owner) viewport(object, model, depth); break;
            default: break;
        }
    }

    void writeSpace(const std::string &name, Dwg_Object *space, Dwg_Object *model, bool paper) {
        out.raw("{\"name\":"); out.quote(name); out.raw(",\"entities\":[");
        firstEntity = true;
        block(space, {}, {}, "", 0xffffff, nullptr, 0, paper ? model : nullptr);
        out.raw("]}");
    }
};

std::string readDrawing(const char *path) {
    Dwg_Data drawing{};
    int error = dwg_read_file(path, &drawing);
    if (error >= DWG_ERR_CRITICAL) {
        dwg_free(&drawing);
        throw std::runtime_error("DWG 文件损坏或版本不受支持");
    }
    dwg_api_init_version(&drawing);
    try {
        Reader reader(&drawing);
        std::string result = reader.convert();
        dwg_free(&drawing);
        return result;
    } catch (...) {
        dwg_free(&drawing);
        throw;
    }
}
} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_infomatik_dwgviewer_NativeBridge_readDrawing(JNIEnv *env, jobject, jstring filePath) {
    if (!filePath) return env->NewStringUTF("{\"error\":\"文件路径为空\"}");
    const char *path = env->GetStringUTFChars(filePath, nullptr);
    if (!path) return nullptr;
    std::string result;
    try {
        result = readDrawing(path);
    } catch (const std::exception &error) {
        Json json;
        json.raw("{\"error\":"); json.quote(error.what()); json.raw("}");
        result = std::move(json.value);
    }
    env->ReleaseStringUTFChars(filePath, path);
    return env->NewStringUTF(result.c_str());
}
