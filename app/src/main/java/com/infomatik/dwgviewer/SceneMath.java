package com.infomatik.dwgviewer;

public final class SceneMath {
    private SceneMath() {}

    public static double fitScale(double minX, double minY, double maxX, double maxY,
                                  double width, double height, double padding) {
        double drawingWidth = Math.max(1e-9, maxX - minX);
        double drawingHeight = Math.max(1e-9, maxY - minY);
        return Math.max(1e-9, Math.min((width - 2 * padding) / drawingWidth,
                                      (height - 2 * padding) / drawingHeight));
    }

    public static double segmentDistance(double x, double y, double x1, double y1,
                                         double x2, double y2) {
        double dx = x2 - x1;
        double dy = y2 - y1;
        double lengthSquared = dx * dx + dy * dy;
        if (lengthSquared == 0) return Math.hypot(x - x1, y - y1);
        double t = Math.max(0, Math.min(1, ((x - x1) * dx + (y - y1) * dy) / lengthSquared));
        return Math.hypot(x - (x1 + t * dx), y - (y1 + t * dy));
    }

    public static boolean pointInPolygon(double x, double y, float[] points) {
        int count = points.length / 2;
        boolean inside = false;
        for (int i = 0, j = count - 1; i < count; j = i++) {
            double xi = points[i * 2], yi = points[i * 2 + 1];
            double xj = points[j * 2], yj = points[j * 2 + 1];
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi)
                inside = !inside;
        }
        return inside;
    }
}
