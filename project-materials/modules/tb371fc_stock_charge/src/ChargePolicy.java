/** Native charging target plus an independent fourth bypass mode. */
public final class ChargePolicy {
    private static final int MIN_TARGET=20;
    private static final int NATIVE_RELEASE_GAP=2;
    private static final int BYPASS_STOP=20;
    private static final int BYPASS_RESUME=25;
    private int previousMode=-1,previousTarget=-1;
    private boolean nativeHold,bypassArmed;

    public boolean stop(int mode,int soc,boolean external,boolean interactive,
                        int nativeTarget,boolean bypassAllowed) {
        if(mode!=previousMode){nativeHold=false;bypassArmed=false;previousTarget=-1;}
        previousMode=mode;
        if(!external||soc<0||soc>100||mode<0||mode>3){
            nativeHold=false;bypassArmed=false;previousTarget=-1;return false;
        }
        updateNativeHold(nativeTarget,soc);
        if(mode!=3){bypassArmed=false;return nativeHold;}
        if(mode==3){
            if(soc>=BYPASS_RESUME)bypassArmed=true;
            if(soc<=BYPASS_STOP)bypassArmed=false;
            boolean bypassHold=interactive&&bypassArmed&&bypassAllowed;
            // Native protection is a hard ceiling; bypass may stop earlier but cannot release it.
            return nativeHold||bypassHold;
        }
        return nativeHold;
    }

    private void updateNativeHold(int target,int soc) {
        if(target<MIN_TARGET||target>100){
            nativeHold=false;previousTarget=-1;return;
        }
        if(target!=previousTarget)nativeHold=false;
        previousTarget=target;
        if(soc>=target)nativeHold=true;
        else if(soc<=target-NATIVE_RELEASE_GAP)nativeHold=false;
    }
}
