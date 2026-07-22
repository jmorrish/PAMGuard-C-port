package org.pamguard.port.reference;

import PamguardMVC.DataUnitBaseData;
import clickDetector.ClickDetection;
import clickDetector.echoDetection.SimpleEchoDetectionSystem;
import clickDetector.echoDetection.SimpleEchoDetector;
import clickDetector.echoDetection.SimpleEchoParams;

import java.io.File;
import java.io.PrintWriter;
import java.lang.reflect.Constructor;
import java.lang.reflect.Field;
import java.util.Locale;

/**
 * Exports SimpleEchoDetector fixtures by driving the REAL detector class.
 *
 * The detector's constructor wants ClickControl and ChannelGroupDetector
 * (PamController-coupled), but isEcho itself reads only its private
 * sampleRate, the system's SimpleEchoParams, and the click's start sample.
 * So the detector and its system are allocated the way deserialisation
 * allocates (no constructor), the three fields it reads are set reflectively,
 * and the logic that runs is PAMGuard's own bytecode, unmodified.
 *
 * ClickDetection instances get a real DataUnitBaseData carrying the start
 * sample, which is all isEcho touches.
 *
 * Scenarios are shared by name with the C++ check
 * (cpp-engine/tools/simple_echo_fixture_check.cpp).
 */
public final class SimpleEchoFixtureExporter {

    private static final class EchoScenario {
        String name;
        double sampleRateHz;
        double maxIntervalSeconds;
        long[] startSamples;

        EchoScenario(String name, double sampleRateHz, double maxIntervalSeconds, long[] startSamples) {
            this.name = name;
            this.sampleRateHz = sampleRateHz;
            this.maxIntervalSeconds = maxIntervalSeconds;
            this.startSamples = startSamples;
        }
    }

    private SimpleEchoFixtureExporter() {
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 1) {
            System.err.println("Usage: SimpleEchoFixtureExporter <output.csv>");
            System.exit(2);
        }

        Locale.setDefault(Locale.ROOT);
        File output = new File(args[0]);
        output.getParentFile().mkdirs();

        try (PrintWriter writer = new PrintWriter(output)) {
            writer.println("scenario,sampleRateHz,maxIntervalSeconds,startSample,isEcho");
            for (EchoScenario scenario : scenarioCatalogue()) {
                SimpleEchoDetector detector = buildDetector(scenario.sampleRateHz, scenario.maxIntervalSeconds);
                for (long startSample : scenario.startSamples) {
                    boolean echo = detector.isEcho(clickAt(startSample));
                    writer.printf(Locale.ROOT, "%s,%.17g,%.17g,%d,%d%n",
                            scenario.name, scenario.sampleRateHz, scenario.maxIntervalSeconds,
                            startSample, echo ? 1 : 0);
                }
            }
        }
    }

    private static SimpleEchoDetector buildDetector(double sampleRateHz, double maxIntervalSeconds) throws Exception {
        SimpleEchoParams params = new SimpleEchoParams();
        params.maxIntervalSeconds = maxIntervalSeconds;

        SimpleEchoDetectionSystem system = newWithoutConstructor(SimpleEchoDetectionSystem.class);
        setField(system, SimpleEchoDetectionSystem.class, "simpleEchoParams", params);

        SimpleEchoDetector detector = newWithoutConstructor(SimpleEchoDetector.class);
        setField(detector, SimpleEchoDetector.class, "simpleEchoDetectionSystem", system);
        // Pre-setting a non-zero sample rate keeps isEcho off its initialise()
        // path, which is the one place it would reach ClickControl.
        setField(detector, SimpleEchoDetector.class, "sampleRate", sampleRateHz);
        return detector;
    }

    private static ClickDetection clickAt(long startSample) throws Exception {
        ClickDetection click = newWithoutConstructor(ClickDetection.class);
        DataUnitBaseData basicData = new DataUnitBaseData();
        basicData.setStartSample(startSample);
        setField(click, Class.forName("PamguardMVC.PamDataUnit"), "basicData", basicData);
        return click;
    }

    @SuppressWarnings("unchecked")
    private static <T> T newWithoutConstructor(Class<T> type) throws Exception {
        Constructor<?> objectConstructor = Object.class.getDeclaredConstructor();
        Constructor<?> allocator = sun.reflect.ReflectionFactory.getReflectionFactory()
                .newConstructorForSerialization(type, objectConstructor);
        return (T) allocator.newInstance();
    }

    private static void setField(Object target, Class<?> declaringClass, String fieldName, Object value)
            throws Exception {
        Field field = declaringClass.getDeclaredField(fieldName);
        field.setAccessible(true);
        field.set(target, value);
    }

    private static EchoScenario[] scenarioCatalogue() {
        return new EchoScenario[]{
                // First click never an echo; 0.1 s at 48 kHz is 4800 samples.
                new EchoScenario("first-click", 48000.0, 0.1, new long[]{1000}),
                // The boundary is inclusive: delay of exactly maxInterval is
                // an echo; one sample past it is not.
                new EchoScenario("inclusive-boundary", 48000.0, 0.1,
                        new long[]{1000, 5800, 5801, 10602}),
                // The anchor only advances on non-echoes: a burst measures
                // every member against the original click, and the first one
                // past the window becomes the new anchor.
                new EchoScenario("burst-anchoring", 48000.0, 0.1,
                        new long[]{1000, 3000, 5000, 5799, 7000, 11801}),
                // Negative delay: an out-of-order click is not an echo and
                // becomes the anchor.
                new EchoScenario("out-of-order", 48000.0, 0.1,
                        new long[]{10000, 2000, 3000, 40000}),
                // Different rate and window.
                new EchoScenario("slow-rate-long-window", 8000.0, 0.25,
                        new long[]{0, 2000, 2001, 4002, 8000}),
                // Zero interval: only an identical start sample is an echo.
                new EchoScenario("zero-interval", 48000.0, 0.0,
                        new long[]{500, 500, 501}),
        };
    }
}
