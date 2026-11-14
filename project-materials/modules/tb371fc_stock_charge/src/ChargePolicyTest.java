public final class ChargePolicyTest {
    static final int HEALTH=2, BYPASS=1;
    static void check(boolean value, String label) { if (!value) throw new AssertionError(label); }
    public static void main(String[] args) {
        ChargePolicy p = new ChargePolicy();
        check(p.stop(BYPASS,64,true,false), "Immediate bypass below 80%");
        check(!p.stop(BYPASS,64,false,false), "Disconnect releases USER stop");
        check(!p.stop(0,90,true,false), "Normal charge");
        check(!p.stop(HEALTH,79,true,false), "Health below cap");
        check(p.stop(HEALTH,80,true,false), "Health cap");
        check(p.stop(HEALTH,79,true,false), "Hysteresis");
        check(!p.stop(HEALTH,78,true,false), "Health restart");
        check(p.stop(HEALTH,81,true,false), "Health cap again");
        check(!p.stop(HEALTH,85,true,true), "Honor Xiaomi once-full request");
        check(p.stop(HEALTH,85,true,false), "Once-full ends");
        check(!p.stop(0,85,true,false), "Mode change releases stop");
        check(!p.stop(HEALTH,79,true,false), "No stale health state on mode reentry");
        check(!p.stop(BYPASS,-1,true,false), "Bad SOC releases module vote");
        check(!p.stop(3,75,true,false), "Reject unknown mode");
        System.out.println("PASS 14 charging state transitions");
    }
}
