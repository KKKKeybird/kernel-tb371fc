/** Pure policy. No temperature override, USB suspension, or fake SOC. */
public final class ChargePolicy {
    private boolean healthHold;
    private int previousMode = -1;
    public boolean stop(int mode, int soc, boolean externalPower, boolean onceFull) {
        if (mode != previousMode) healthHold = false;
        previousMode = mode;
        if (!externalPower || soc < 0 || soc > 100 || mode < 0 || mode > 2) {
            healthHold = false;
            return false;
        }
        if (mode == 1) return true;
        if (mode != 2 || onceFull) { healthHold = false; return false; }
        if (soc >= 80) healthHold = true;
        else if (soc <= 78) healthHold = false;
        return healthHold;
    }
}
