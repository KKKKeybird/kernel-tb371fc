import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;

/** Xiaomi settings -> Lenovo's stock charge_disable hardware interface. */
public final class ChargeBridge {
    static final String KEY = "security_pc_secure_protect_mode_key";
    static final String B = "/sys/class/power_supply/battery/";
    static final ChargePolicy policy = new ChargePolicy();
    static Path module, runtime;
    static Object resolver;
    static Object settingsToken;
    static Class<?> resolverType, uriType, bundleType, propsType;
    static Method getMode, propGet, propSet;
    static int lastWritten = -1, onceState = -1;
    static long lastProvider, holdSince;
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
        if (now - lastProvider < 30_000_000_000L) return;
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
        if (!enabled() || !supervisorAlive()) System.exit(0);
        // bp_enable shares the USER voter: never undo an active Lenovo protection owner.
        if (number(B + "bp_enable") != 0) throw new IOException("Conflicting Lenovo bp_enable owner");
        int mode = (Integer) getMode.invoke(null, resolver, KEY, -1, 0);
        // Only bootstrap from the legacy property if the real Xiaomi key is absent.
        if (mode == -1) {
            String fallback = property("persist.sys.pc_mode");
            mode = fallback.isEmpty() ? 0 : Integer.parseInt(fallback);
        }
        if (mode < 0 || mode > 2) throw new IOException("Unsupported mode " + mode);
        // Keep the existing ROM's status readers consistent; this is not the control source.
        if (!Integer.toString(mode).equals(property("persist.sys.pc_mode")))
            propSet.invoke(null, "persist.sys.pc_mode", Integer.toString(mode));
        int soc = number(B + "capacity");
        boolean plugged = number("/sys/class/power_supply/usb/present") == 1
            || number("/sys/class/power_supply/dc/present") == 1;
        if (mode == 2) onceFullState();
        else { onceState = -1; lastProvider = 0; }
        boolean hold = policy.stop(mode, soc, plugged, onceState == 1);
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
        String state = "mode=" + mode + " soc=" + soc + " external=" + plugged
            + " hold=" + hold + " once_full=" + onceState
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
        module = Paths.get(args[0]); runtime = Paths.get(args[1]);
        Class<?> looper = Class.forName("android.os.Looper");
        looper.getMethod("prepareMainLooper").invoke(null);
        Class<?> at = Class.forName("android.app.ActivityThread");
        Object thread = at.getMethod("systemMain").invoke(null);
        Object context = at.getMethod("getSystemContext").invoke(thread);
        Class<?> contextType = Class.forName("android.content.Context");
        resolver = contextType.getMethod("getContentResolver").invoke(context);
        resolverType = Class.forName("android.content.ContentResolver");
        uriType = Class.forName("android.net.Uri"); bundleType = Class.forName("android.os.Bundle");
        propsType = Class.forName("android.os.SystemProperties");
        propGet = propsType.getMethod("get", String.class, String.class);
        propSet = propsType.getMethod("set", String.class, String.class);
        // app_process is not registered as an Android application. Acquire the
        // settings provider through the same external-provider route as settings(1).
        Object am = Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
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
        /* No ContentObserver: R8 rejects unmanaged app_process PIDs. */
        Runtime.getRuntime().addShutdownHook(new Thread(() -> {
            try {
                if (number(B + "bp_enable") == 0) write(B + "charge_disable", "0");
                if (!supervisorAlive()) propSet.invoke(null, "ctl.start", "lwky_smart_charge");
            } catch (Exception e) { e.printStackTrace(); }
        }));
        initialized = true;
        System.out.println("READY same-process settings polling; interval=1s; no wake lock");
        while (enabled()) {
            evaluate("settings");
            Thread.sleep(1000);
        }
    }
}
