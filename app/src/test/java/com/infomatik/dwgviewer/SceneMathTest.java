package com.infomatik.dwgviewer;

import org.junit.Test;

public final class SceneMathTest {
    @Test public void geometry() {
        close(SceneMath.fitScale(0, 0, 100, 50, 200, 200, 10), 1.8);
        close(SceneMath.segmentDistance(5, 4, 0, 0, 10, 0), 4.0);
        close(SceneMath.segmentDistance(20, 0, 0, 0, 10, 0), 10.0);
        close(SceneMath.segmentDistance(3, 4, 0, 0, 0, 0), 5.0);
        float[] square = {0, 0, 10, 0, 10, 10, 0, 10};
        if (!SceneMath.pointInPolygon(5, 5, square)) throw new AssertionError("inside fill");
        if (SceneMath.pointInPolygon(15, 5, square)) throw new AssertionError("outside fill");
    }

    private static void close(double actual, double expected) {
        if (Math.abs(actual - expected) > 1e-9) {
            throw new AssertionError("expected " + expected + ", got " + actual);
        }
    }
}
