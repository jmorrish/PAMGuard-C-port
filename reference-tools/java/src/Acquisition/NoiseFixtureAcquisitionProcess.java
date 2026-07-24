package Acquisition;

/**
 * Constructor-free fixture seam for noiseMonitor.NoiseProcess. It leaves the
 * real band integration and statistics code untouched and makes only the
 * final hardware calibration step an identity function.
 */
public class NoiseFixtureAcquisitionProcess extends AcquisitionProcess {
    protected NoiseFixtureAcquisitionProcess() {
        super(null, true);
    }

    @Override
    public double prepareFastAmplitudeCalculation(int channel) {
        return 0.0;
    }

    @Override
    public double fftBandAmplitude2dB(double amplitude, int channel,
            int fftLength, boolean isSquared, boolean fast) {
        return amplitude;
    }
}
