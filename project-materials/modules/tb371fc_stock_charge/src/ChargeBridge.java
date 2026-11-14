import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import android.os.Binder;
import android.os.Parcel;
import android.os.RemoteException;

/** Xiaomi settings -> Lenovo's stock charge_disable hardware interface. */
public final class ChargeBridge {
    static final String KEY = "security_pc_secure_protect_mode_key";
    static final String B = "/sys/class/power_supply/battery/";
    static final int FALLBACK_HEALTH_TARGET = 80;
    static final String RESET_CONTROLS = "--reset-controls";
    static final String[] RESET_INT_CONTROLS = {
        "tb371fc_native_manager_initialized",
        "tb371fc_smart_common_limit",
        "tb371fc_smart_navigation_limit",
        "tb371fc_smart_night",
        "tb371fc_smart_command",
        "tb371fc_smart_applied",
        "tb371fc_charge_hold",
        "tb371fc_once_full_state"
    };
    static final String[] RESET_LONG_CONTROLS = {
        "tb371fc_native_manager_time",
        "tb371fc_smart_request_time",
        "tb371fc_once_full_time"
    };
    static final ChargePolicy policy = new ChargePolicy();
    static Path module, runtime;
    static Object resolver;
    static Object settingsToken;
    static Class<?> resolverType, uriType, bundleType, propsType;
    static Method getMode, propGet, propSet, putSetting, getLong;
    static Object power, keyguard, users;
    static Method isUserUnlocked;
    static volatile int currentMode;
    static Method interactiveMethod, lockedMethod;
    static long bootEpoch, drainSince, backoffUntil;
    static int lastApplied=-1,lastHold=-1;
    static final Object signal=new Object();
    static ScreenReceiver screenReceiver;
    static Object activityManager;
    static Object packageManager;
    static Method appInfo;
    static long lastPackageCheck;
    static int lastWritten = -1;
    static volatile int onceState = -1;
    static long lastProvider, holdSince;
    static boolean previousPlugged;
    static long chargeSession;
    static String lastState = "";
    static volatile boolean initialized;
    static String read(String p) throws IOException {
        return new String(Files.readAllBytes(Paths.get(p)), StandardCharsets.UTF_8).trim();
    }
    static int number(String p) throws IOException { return Integer.parseInt(read(p)); }
    static void write(String p, String v) throws IOException {
        // O_TRUNC on sysfs is conventional. Never replace the node or bind a fake value.
        try (FileOutputStream out = new FileOutputStream(p)) {
            out.write((v + "\n").getBytes(StandardCharsets.US_ASCII));
        }
    }
    static String property(String key) throws Exception {
        return (String) propGet.invoke(null, key, "");
    }
    static void resetPrivateControls() throws Exception {
        Method putLong = Class.forName("android.provider.Settings$Secure").getMethod(
            "putLongForUser", resolverType, String.class, long.class, int.class);
        for(String key : RESET_INT_CONTROLS) {
            if(!Boolean.TRUE.equals(putSetting.invoke(null, resolver, key, 0, 0)))
                throw new IOException("Cannot reset " + key);
        }
        for(String key : RESET_LONG_CONTROLS) {
            if(!Boolean.TRUE.equals(putLong.invoke(null, resolver, key, 0L, 0)))
                throw new IOException("Cannot reset " + key);
        }
        System.out.println("RESET_CONTROLS ok");
    }
    public static final class ScreenReceiver extends Binder {
        @Override protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws RemoteException {
            if(code==1){
                try{
                    Class<?> parcel=Class.forName("android.os.Parcel");
                    parcel.getMethod("enforceInterface",String.class).invoke(data,"android.content.IIntentReceiver");
                    Class<?> creator=Class.forName("android.os.Parcelable$Creator");
                    Method typed=parcel.getMethod("readTypedObject",creator);
                    Object intent=typed.invoke(data,Class.forName("android.content.Intent").getField("CREATOR").get(null));
                    int resultCode=(Integer)parcel.getMethod("readInt").invoke(data);
                    String resultData=(String)parcel.getMethod("readString").invoke(data);
                    Object extras=typed.invoke(data,Class.forName("android.os.Bundle").getField("CREATOR").get(null));
                    boolean ordered=(Boolean)parcel.getMethod("readBoolean").invoke(data);
                    parcel.getMethod("readBoolean").invoke(data);
                    parcel.getMethod("readInt").invoke(data);
                    if(ordered){
                        int intentFlags=intent==null?0:(Integer)intent.getClass().getMethod("getFlags").invoke(intent);
                        Class.forName("android.app.IActivityManager").getMethod("finishReceiver",Class.forName("android.os.IBinder"),int.class,String.class,Class.forName("android.os.Bundle"),boolean.class,int.class)
                            .invoke(activityManager,this,resultCode,resultData,extras,false,intentFlags);
                    }
                    synchronized(signal){signal.notifyAll();}
                    return true;
                }catch(Exception e){System.err.println("SCREEN_EVENT "+e);return false;}
            }
            return super.onTransact(code,data,reply,flags);
        }
    }
    static int setting(String key,int fallback)throws Exception {
        return (Integer)getMode.invoke(null,resolver,key,fallback,0);
    }
    static int smartLimit()throws Exception {
        long time=(Long)getLong.invoke(null,resolver,"tb371fc_smart_request_time",0L,0);
        if(time<bootEpoch)return 0;
        int limit=0;
        for(String key:new String[]{"tb371fc_smart_common_limit","tb371fc_smart_navigation_limit"}){
            int next=setting(key,0);
            if(next>=20&&next<=100&&(limit==0||next<limit))limit=next;
        }
        if(setting("tb371fc_smart_night",0)==1&&(limit==0||limit>80))limit=80;
        return limit;
    }
    static boolean supervisorAlive() {
        try {
            String pid = read(runtime.resolve("pid").toString());
            return pid.matches("[0-9]+") && read("/proc/" + pid + "/cmdline").contains(module.resolve("supervisor.sh").toString());
        } catch (IOException e) { return false; }
    }
    static boolean enabled() {
        return !Files.exists(module.resolve("disable")) && !Files.exists(module.resolve("remove"));
    }
    static void onceFullState() {
        long now = System.nanoTime();
        if (now - lastProvider < (currentMode==2?2_000_000_000L:10_000_000_000L)) return;
        lastProvider = now;
        Object token = null, am = null;
        try {
            Class<?> amType = Class.forName("android.app.IActivityManager");
            Class<?> binderType = Class.forName("android.os.IBinder");
            am = Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
            token = Class.forName("android.os.Binder").getConstructor().newInstance();
            Object holder = amType.getMethod("getContentProviderExternal", String.class, int.class, binderType, String.class)
                .invoke(am, "com.miui.powercenter.provider", 0, token, "tb371fc-charge-bridge");
            if (holder == null) { onceState = -1; return; }
            Object provider = holder.getClass().getField("provider").get(holder);
            Object attribution = resolverType.getMethod("getAttributionSource").invoke(resolver);
            Object result = Class.forName("android.content.IContentProvider").getMethod("call",
                Class.forName("android.content.AttributionSource"), String.class, String.class, String.class, bundleType)
                .invoke(provider, attribution, "com.miui.powercenter.provider", "getThisTimeNoProtect", null, null);
            onceState = result == null ? -1 : (Integer) bundleType.getMethod("getInt", String.class, int.class)
                .invoke(result, "once_no_protect", -1);
        } catch (Exception e) {
            onceState = -1;
            System.err.println("MIUI_ONCE_FULL_UNAVAILABLE " + e);
        } finally {
            if (token != null && am != null) try {
                Class.forName("android.app.IActivityManager").getMethod("removeContentProviderExternal",
                    String.class, Class.forName("android.os.IBinder"))
                    .invoke(am, "com.miui.powercenter.provider", token);
            } catch (Exception e) { System.err.println("PROVIDER_RELEASE " + e); }
        }
    }
    static synchronized void evaluate(String reason) throws Exception {
        if (!initialized) return;
        long checkTime=System.nanoTime();
        if(checkTime-lastPackageCheck>10_000_000_000L){
            Object info=appInfo.invoke(packageManager,"com.miui.securitycenter",0);
            String source=(String)info.getClass().getField("sourceDir").get(info);
            if(!new File(source).getCanonicalPath().equals(new File("/product/priv-app/MIUISecurityCenterPad/MIUISecurityCenterPad.apk").getCanonicalPath()))
                throw new IOException("Security Center package updated; reverting old patch");
            lastPackageCheck=checkTime;
        }
        if (!enabled() || !supervisorAlive()) System.exit(0);
        // bp_enable shares the USER voter: never undo an active Lenovo protection owner.
        if (number(B + "bp_enable") != 0) throw new IOException("Conflicting Lenovo bp_enable owner");
        int mode = (Integer) getMode.invoke(null, resolver, KEY, -1, 0);
        // Only bootstrap from the legacy property if the real Xiaomi key is absent.
        if (mode == -1) {
            String fallback = property("persist.sys.pc_mode");
            mode = fallback.isEmpty() ? 0 : Integer.parseInt(fallback);
        }
        if (mode < 0 || mode > 3) throw new IOException("Unsupported mode " + mode);
        // Keep the existing ROM's status readers consistent; this is not the control source.
        if (!Integer.toString(mode).equals(property("persist.sys.pc_mode")))
            propSet.invoke(null, "persist.sys.pc_mode", Integer.toString(mode));
        int soc = number(B + "capacity");
        boolean plugged = number("/sys/class/power_supply/usb/present") == 1
            || number("/sys/class/power_supply/dc/present") == 1;
        currentMode=mode;
        if(chargeSession==0&&plugged){chargeSession=bootEpoch;previousPlugged=true;}
        else if(plugged!=previousPlugged){chargeSession=System.currentTimeMillis();previousPlugged=plugged;backoffUntil=0;}
        long onceTime=(Long)getLong.invoke(null,resolver,"tb371fc_once_full_time",0L,0);
        boolean onceFull=setting("tb371fc_once_full_state",-1)==1&&onceTime>=bootEpoch&&onceTime>=chargeSession;
        long managerTime=(Long)getLong.invoke(null,resolver,"tb371fc_native_manager_time",0L,0);
        boolean nativeReady=setting("tb371fc_native_manager_initialized",0)==1&&managerTime>=bootEpoch;
        boolean interactive=(Boolean)interactiveMethod.invoke(power)&&!(Boolean)lockedMethod.invoke(keyguard);
        int nativeTarget=0;
        String targetSource="none";
        if(nativeReady){
            // The native manager owns all three stock modes; its calculated target is authoritative.
            nativeTarget=smartLimit();
            targetSource=nativeTarget==0?"native-clear":"native";
        }else if(mode==2){
            // 80 is only a temporary health fallback while the native manager is unavailable.
            if(onceFull)targetSource="fallback-cleared";
            else {nativeTarget=FALLBACK_HEALTH_TARGET;targetSource="fallback";}
        }
        int effectiveMode=mode;
        long now=System.nanoTime();
        int observedCurrent=number(B+"current_now");
        if(effectiveMode==3&&lastWritten==1&&observedCurrent>200000){
            if(drainSince==0)drainSince=now;
            if(now-drainSince>=15_000_000_000L){backoffUntil=now+60_000_000_000L;drainSince=0;}
        }else drainSince=0;
        boolean hold=policy.stop(effectiveMode,soc,plugged,interactive,nativeTarget,now>=backoffUntil);
        int wanted = hold ? 1 : 0;
        if (hold && holdSince == 0) holdSince = System.nanoTime();
        if (!hold) holdSince = 0;
        // Re-assert only while we own a stop request and hardware unexpectedly re-enabled.
        if (wanted != lastWritten || (hold && number(B + "charging_enabled") != 0)) {
            write(B + "charge_disable", Integer.toString(wanted));
            lastWritten = wanted;
        }
        int pump = number("/sys/class/power_supply/charge_pump_master/cp_switcher_en");
        int batteryCurrent = number(B + "current_now");
        if (hold && System.nanoTime() - holdSince > 15_000_000_000L
                && (pump != 0 || batteryCurrent < -200000))
            throw new IOException("Stock stop did not settle; cp=" + pump + " battery_ua=" + batteryCurrent);
        int applied=mode==1&&nativeReady&&nativeTarget>0&&setting("tb371fc_smart_night",0)==1?1:0;
        if(applied!=lastApplied){putSetting.invoke(null,resolver,"tb371fc_smart_applied",applied,0);lastApplied=applied;}
        int holdValue=hold?1:0;
        if(holdValue!=lastHold){putSetting.invoke(null,resolver,"tb371fc_charge_hold",holdValue,0);lastHold=holdValue;}
        String state = "mode=" + mode + " effective_mode=" + effectiveMode + " soc=" + soc + " external=" + plugged
            + " interactive=" + interactive + " smart_limit=" + nativeTarget
            + " target_source=" + targetSource
            + " native_ready=" + nativeReady + " bypass_backoff=" + (now<backoffUntil) + " hold=" + hold + " once_full=" + onceState
            + " input_suspend=" + read(B + "input_suspend")
            + " thermal_level=" + read(B + "charge_control_limit")
            + " charging_enabled=" + read(B + "charging_enabled")
            + " cp_switcher=" + pump + " battery_ua=" + batteryCurrent;
        Path tmp = runtime.resolve("status.tmp");
        Files.write(tmp, ("time=" + System.currentTimeMillis() + " " + state + "\n").getBytes(StandardCharsets.UTF_8));
        Files.move(tmp, runtime.resolve("status"), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        String decision = state.substring(0, state.indexOf(" battery_ua="));
        if (!decision.equals(lastState)) {
            System.out.println("STATE " + reason + " " + state);
            lastState = decision;
        }
    }
    static void event(String reason) {
        try { evaluate(reason); }
        catch (Exception e) { e.printStackTrace(); System.exit(1); }
    }
    public static void main(String[] args) throws Exception {
        boolean resetControls = args.length > 0 && RESET_CONTROLS.equals(args[0]);
        int offset = resetControls ? 1 : 0;
        if(args.length != offset + 2)
            throw new IllegalArgumentException("Usage: ChargeBridge [--reset-controls] <module> <runtime>");
        module = Paths.get(args[offset]); runtime = Paths.get(args[offset + 1]);
        Class<?> looper = Class.forName("android.os.Looper");
        looper.getMethod("prepareMainLooper").invoke(null);
        Class<?> at = Class.forName("android.app.ActivityThread");
        Object thread = at.getMethod("systemMain").invoke(null);
        Object context = at.getMethod("getSystemContext").invoke(thread);
        Class<?> contextType = Class.forName("android.content.Context");
        resolver = contextType.getMethod("getContentResolver").invoke(context);
        packageManager=contextType.getMethod("getPackageManager").invoke(context);
        appInfo=Class.forName("android.content.pm.PackageManager").getMethod("getApplicationInfo",String.class,int.class);
        resolverType = Class.forName("android.content.ContentResolver");
        uriType = Class.forName("android.net.Uri"); bundleType = Class.forName("android.os.Bundle");
        propsType = Class.forName("android.os.SystemProperties");
        propGet = propsType.getMethod("get", String.class, String.class);
        propSet = propsType.getMethod("set", String.class, String.class);
        // app_process is not registered as an Android application. Acquire the
        // settings provider through the same external-provider route as settings(1).
        Object am = Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
        activityManager=am;
        Class<?> binderType = Class.forName("android.os.IBinder");
        settingsToken = Class.forName("android.os.Binder").getConstructor().newInstance();
        Class<?> amType = Class.forName("android.app.IActivityManager");
        Object holder = amType.getMethod("getContentProviderExternal", String.class, int.class, binderType, String.class)
            .invoke(am, "settings", 0, settingsToken, "tb371fc-charge-bridge");
        Object provider = holder.getClass().getField("provider").get(holder);
        Class<?> secure = Class.forName("android.provider.Settings$Secure");
        Field providerHolderField = secure.getDeclaredField("sProviderHolder");
        providerHolderField.setAccessible(true);
        Object providerHolder = providerHolderField.get(null);
        Field providerField = providerHolder.getClass().getDeclaredField("mContentProvider");
        providerField.setAccessible(true); providerField.set(providerHolder, provider);
        getMode = secure.getMethod("getIntForUser", resolverType, String.class, int.class, int.class);
        putSetting=secure.getMethod("putIntForUser",resolverType,String.class,int.class,int.class);
        getLong=secure.getMethod("getLongForUser",resolverType,String.class,long.class,int.class);
        if(resetControls) {
            resetPrivateControls();
            return;
        }
        bootEpoch=System.currentTimeMillis()-(Long)Class.forName("android.os.SystemClock").getMethod("elapsedRealtime").invoke(null);
        power=contextType.getMethod("getSystemService",String.class).invoke(context,"power");
        keyguard=contextType.getMethod("getSystemService",String.class).invoke(context,"keyguard");
        users=contextType.getMethod("getSystemService",String.class).invoke(context,"user");
        isUserUnlocked=Class.forName("android.os.UserManager").getMethod("isUserUnlocked");
        interactiveMethod=Class.forName("android.os.PowerManager").getMethod("isInteractive");
        lockedMethod=Class.forName("android.app.KeyguardManager").getMethod("isKeyguardLocked");
        // Register with a null ApplicationThread: root app_process has no AMS app record.
        screenReceiver=new ScreenReceiver();
        Object receiver=Class.forName("android.content.IIntentReceiver$Stub").getMethod("asInterface",binderType).invoke(null,screenReceiver);
        Class<?> filterType=Class.forName("android.content.IntentFilter");
        Object filter=filterType.getConstructor().newInstance();
        for(String action:new String[]{"android.intent.action.SCREEN_ON","android.intent.action.SCREEN_OFF","android.intent.action.ACTION_POWER_CONNECTED","android.intent.action.ACTION_POWER_DISCONNECTED"})
            filterType.getMethod("addAction",String.class).invoke(filter,action);
        amType.getMethod("registerReceiverWithFeature",Class.forName("android.app.IApplicationThread"),String.class,String.class,String.class,
            Class.forName("android.content.IIntentReceiver"),filterType,String.class,int.class,int.class)
            .invoke(am,null,"android",null,"tb371fc-charge",receiver,filter,null,0,2);
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            try {
                if (number(B + "bp_enable") == 0) write(B + "charge_disable", "0");
                putSetting.invoke(null,resolver,"tb371fc_smart_applied",0,0);
                putSetting.invoke(null,resolver,"tb371fc_charge_hold",0,0);
                if(!supervisorAlive())new ProcessBuilder("/system/bin/sh",module.resolve("rollback.sh").toString(),"--orphan").start().waitFor();


            } catch (Exception e) { e.printStackTrace(); }
        }));
        initialized = true;
        Thread nativeProbe=new Thread(()->{
            while(enabled()&&supervisorAlive()){
                try{if((Boolean)isUserUnlocked.invoke(users))onceFullState();Thread.sleep(1000);}
                catch(Exception e){System.err.println("NATIVE_PROBE "+e);try{Thread.sleep(5000);}catch(InterruptedException ignored){return;}}
            }
        },"native-charge-probe");
        nativeProbe.setDaemon(true);nativeProbe.setPriority(Thread.MIN_PRIORITY);nativeProbe.start();
        System.out.println("READY same-process settings polling; interval=1s; no wake lock");
        while (enabled()) {
            evaluate("settings");
            synchronized(signal){signal.wait(1000);}
        }
    }
}
