import java.lang.reflect.*;
import java.util.*;
/** Loaded inside the existing Security Center process; no replacement Activity. */
public final class ChargeUi {
    public static final String MODE="security_pc_secure_protect_mode_key";
    static final String PROVIDER_URI="content://com.miui.powercenter.provider";
    static final String PROVIDER_INIT_METHOD="getSupportNightProtect";
    static final long DIAGNOSTIC_INTERVAL_MS=60_000L;
    static Object context() throws Exception {
        return Class.forName("android.app.ActivityThread").getMethod("currentApplication").invoke(null);
    }
    static Object resolver() throws Exception {
        return Class.forName("android.content.Context").getMethod("getContentResolver").invoke(context());
    }
    static int get(String key,int fallback) throws Exception {
        return (Integer)Class.forName("android.provider.Settings$Secure").getMethod("getInt",Class.forName("android.content.ContentResolver"),String.class,int.class).invoke(null,resolver(),key,fallback);
    }
    public static int systemMode(Object unused) {
        try{
            int mode=get(MODE,0);
            return mode>=0&&mode<=2?mode:0;
        }catch(Exception e){
            System.err.println("TB371FC SYSTEM_MODE "+e);
            return 0;
        }
    }
    static void put(String key,int value) throws Exception {
        boolean ok=(Boolean)Class.forName("android.provider.Settings$Secure").getMethod("putInt",Class.forName("android.content.ContentResolver"),String.class,int.class).invoke(null,resolver(),key,value);
        if(!ok)throw new IllegalStateException("Cannot save "+key);
    }
    static void putText(String key,String value) throws Exception {
        boolean ok=(Boolean)Class.forName("android.provider.Settings$Secure").getMethod("putString",Class.forName("android.content.ContentResolver"),String.class,String.class).invoke(null,resolver(),key,value);
        if(!ok)throw new IllegalStateException("Cannot save "+key);
    }
    static void diagnosticText(String key,String value){
        try{putText(key,value==null||value.length()==0?"unknown":value);}
        catch(Exception e){System.err.println("TB371FC NATIVE_DIAG_TEXT "+key+" "+e);}
    }
    static void diagnosticInt(String key,Integer value){
        try{
            if(value==null)putText(key,"unknown");else put(key,value);
        }catch(Exception e){
            System.err.println("TB371FC NATIVE_DIAG "+key+" "+e);
            try{putText(key,"unknown");}
            catch(Exception ignored){System.err.println("TB371FC NATIVE_DIAG_UNKNOWN "+key+" "+ignored);}
        }
    }
    static void sequence() throws Exception {
        boolean ok=(Boolean)Class.forName("android.provider.Settings$Secure").getMethod("putLong",Class.forName("android.content.ContentResolver"),String.class,long.class).invoke(null,resolver(),"tb371fc_smart_request_time",System.currentTimeMillis());
        if(!ok)throw new IllegalStateException("Cannot save smart request time");
    }
    public static boolean setSmartCommand(String value) {
        try {
            int command=Integer.decode(value);
            int type=command&65535,limit=command>>>16;
            if(type!=0x10&&type!=0x11&&type!=2&&type!=3)throw new IllegalArgumentException("Unknown smart command "+value);
            if(limit!=0&&(limit<20||limit>100))throw new IllegalArgumentException("Invalid smart limit");
            if(type==0x10)put("tb371fc_smart_common_limit",0);
            if(type==0x11)put("tb371fc_smart_common_limit",limit==0?80:limit);
            if(type==2)put("tb371fc_smart_navigation_limit",0);
            if(type==3)put("tb371fc_smart_navigation_limit",limit==0?80:limit);
            put("tb371fc_smart_command",command);sequence();return true;
        } catch(Exception e){System.err.println("TB371FC SMART_REQUEST "+e);return false;}
    }
    public static String getSmartState() {
        try{return Integer.toString(get("tb371fc_smart_applied",0));}catch(Exception e){return "0";}
    }
    public static Boolean setNightState(int state) {
        try {
            if(state!=0&&state!=1) return Boolean.FALSE;
            put("tb371fc_smart_night",state);sequence();return Boolean.TRUE;
        }catch(Exception e){System.err.println("TB371FC NIGHT_REQUEST "+e);return Boolean.FALSE;}
    }
    static boolean managerStarting,managerReadyCheckScheduled,providerRequestInFlight,statusRefreshPending,providerDiagnosticRunning;
    static volatile boolean managerReady;
    static long lastProviderDiagnostic;
    static java.lang.ref.WeakReference<Object> attachedFragment=new java.lang.ref.WeakReference<>(null);
    static int lastOnce=Integer.MIN_VALUE;
    static void putTime(String key)throws Exception {
        Class.forName("android.provider.Settings$Secure").getMethod("putLong",Class.forName("android.content.ContentResolver"),String.class,long.class)
            .invoke(null,resolver(),key,System.currentTimeMillis());
    }
    static synchronized void markManagerReadyFromHandler(Object instance,Class<?> type){
        if(managerReady||instance==null||type==null)return;
        try{
            Field state=type.getDeclaredField("e");state.setAccessible(true);
            if(state.get(instance)==null)return;
            putTime("tb371fc_native_manager_time");
            put("tb371fc_native_manager_initialized",1);
            managerReady=true;
        }catch(Exception e){System.err.println("TB371FC NATIVE_READY "+e);}
    }
    static void scheduleManagerReadyCheck(Object instance,Class<?> type,Object handler){
        synchronized(ChargeUi.class){
            if(managerReady||managerReadyCheckScheduled)return;
            managerReadyCheckScheduled=true;
        }
        Runnable check=()->{
            try{
                markManagerReadyFromHandler(instance,type);
                refreshProviderDiagnostics(managerReady);
            }
            finally{synchronized(ChargeUi.class){managerReadyCheckScheduled=false;}}
        };
        try{
            Class<?> handlerType=Class.forName("android.os.Handler");
            boolean queued=(Boolean)handlerType.getMethod("postDelayed",Runnable.class,long.class)
                .invoke(handler,check,250L);
            if(!queued)throw new IllegalStateException("Manager handler rejected readiness check");
        }catch(Exception e){
            synchronized(ChargeUi.class){managerReadyCheckScheduled=false;}
            System.err.println("TB371FC NATIVE_READY_SCHEDULE "+e);
        }
    }
    static void requestProviderInit(){
        synchronized(ChargeUi.class){
            if(providerRequestInFlight)return;
            providerRequestInFlight=true;
        }
        Thread request=new Thread(()->{
            try{
                Class<?> uriType=Class.forName("android.net.Uri");
                Class<?> resolverType=Class.forName("android.content.ContentResolver");
                Class<?> bundleType=Class.forName("android.os.Bundle");
                Object uri=uriType.getMethod("parse",String.class).invoke(null,PROVIDER_URI);
                resolverType.getMethod("call",uriType,String.class,String.class,bundleType)
                    .invoke(resolver(),uri,PROVIDER_INIT_METHOD,null,null);
            }catch(Exception e){System.err.println("TB371FC PROVIDER_INIT "+e);}
            finally{synchronized(ChargeUi.class){providerRequestInFlight=false;}}
        },"TB371FC-ChargeProviderInit");
        request.setDaemon(true);request.start();
    }
    static void scheduleStatusRefresh(){
        synchronized(ChargeUi.class){
            if(statusRefreshPending||attachedFragment.get()==null)return;
            statusRefreshPending=true;
        }
        try{
            Class<?> looperType=Class.forName("android.os.Looper");
            Class<?> handlerType=Class.forName("android.os.Handler");
            Object looper=looperType.getMethod("getMainLooper").invoke(null);
            Object handler=handlerType.getConstructor(looperType).newInstance(looper);
            Runnable refresh=()->{
                synchronized(ChargeUi.class){statusRefreshPending=false;}
                Object fragment=attachedFragment.get();
                if(fragment==null)return;
                try{
                    Object preference=find(fragment,"battery_status_now");
                    if(preference==null)return;
                    Class<?> type=Class.forName("com.miui.powercenter.nightcharge.ChargeProtectFragment");
                    String value=(String)type.getMethod("getBatteryStatusNow").invoke(null);
                    try{call(preference,"setText",new Class<?>[]{String.class},value);}
                    catch(NoSuchMethodException noText){summary(preference,value);}
                }catch(Exception e){System.err.println("TB371FC STATUS_REFRESH "+e);}
            };
            boolean queued=(Boolean)handlerType.getMethod("postDelayed",Runnable.class,long.class)
                .invoke(handler,refresh,1500L);
            if(!queued)throw new IllegalStateException("Handler rejected status refresh");
        }catch(Exception e){
            synchronized(ChargeUi.class){statusRefreshPending=false;}
            System.err.println("TB371FC STATUS_REFRESH_SCHEDULE "+e);
        }
    }
    static void applySelection(Object group,Object selected){
        try{
            Class<?> preference=Class.forName("androidx.preference.Preference");
            call(group,"y",new Class<?>[]{preference},new Object[]{null});
            int count=(Integer)call(group,"getPreferenceCount",new Class<?>[0]);
            for(int i=0;i<count;i++){
                Object item=call(group,"getPreference",new Class<?>[]{int.class},i);
                call(item,"setChecked",new Class<?>[]{boolean.class},false);
            }
            if(selected!=null)call(group,"y",new Class<?>[]{preference},selected);
        }catch(Exception e){
            System.err.println("TB371FC SELECTION_SYNC "+e);
            e.printStackTrace();
        }
    }
    static void scheduleBypassSelection(){
        if(attachedFragment.get()==null)return;
        try{
            Class<?> looperType=Class.forName("android.os.Looper");
            Class<?> handlerType=Class.forName("android.os.Handler");
            Object looper=looperType.getMethod("getMainLooper").invoke(null);
            Object handler=handlerType.getConstructor(looperType).newInstance(looper);
            Runnable apply=()->{
                Object fragment=attachedFragment.get();
                if(fragment==null)return;
                try{
                    if(get(MODE,0)!=3)return;
                    Object group=find(fragment,"category_features_battery_protect");
                    Object bypass=find(fragment,"tb371fc_bypass_power");
                    if(group!=null&&bypass!=null)applySelection(group,bypass);
                }catch(Exception e){System.err.println("TB371FC BYPASS_SELECTION "+e);}
            };
            boolean queued=(Boolean)handlerType.getMethod("postDelayed",Runnable.class,long.class)
                .invoke(handler,apply,50L);
            if(!queued)throw new IllegalStateException("Handler rejected bypass selection");
        }catch(Exception e){System.err.println("TB371FC BYPASS_SELECTION_SCHEDULE "+e);}
    }
    public static synchronized void ensureChargingManager(){
        if(managerReady)return;
        if(managerStarting)return;
        managerStarting=true;
        try{
            Object ctx=context();
            Object users=Class.forName("android.content.Context").getMethod("getSystemService",String.class).invoke(ctx,"user");
            if(!(Boolean)Class.forName("android.os.UserManager").getMethod("isUserUnlocked").invoke(users))return;
            Class<?> type=Class.forName("com.miui.powercenter.charge.protect.s");
            Method singleton=type.getDeclaredMethod("l");singleton.setAccessible(true);Object instance=singleton.invoke(null);
            Field handler=type.getDeclaredField("d");handler.setAccessible(true);
            Object managerHandler=handler.get(instance);
            if(managerHandler==null){
                Method start=type.getDeclaredMethod("p",Class.forName("android.content.Context"));start.setAccessible(true);start.invoke(instance,ctx);
                managerHandler=handler.get(instance);
            }
            if(!managerReady)scheduleManagerReadyCheck(instance,type,managerHandler);
        }catch(Exception e){System.err.println("TB371FC NATIVE_INIT "+e);e.printStackTrace();}
        finally{managerStarting=false;}
    }
    static void refreshProviderDiagnostics(boolean force){
        long now=System.currentTimeMillis();
        synchronized(ChargeUi.class){
            if(providerDiagnosticRunning)return;
            if(!force&&now-lastProviderDiagnostic<DIAGNOSTIC_INTERVAL_MS)return;
            providerDiagnosticRunning=true;lastProviderDiagnostic=now;
        }
        try{
            String processName=null;
            try{
                Object name=Class.forName("android.app.Application").getMethod("getProcessName").invoke(context());
                if(name!=null)processName=(String)name;
            }catch(Exception e){System.err.println("TB371FC NATIVE_DIAG_PROCESS "+e);}
            diagnosticText("tb371fc_native_process_name",processName);

            Class<?> manager=null;
            Object instance=null;
            try{
                manager=Class.forName("com.miui.powercenter.charge.protect.NightChargeProtectManager");
                Method singleton=manager.getDeclaredMethod("S");singleton.setAccessible(true);
                instance=singleton.invoke(null);
            }catch(Exception e){System.err.println("TB371FC NATIVE_DIAG_MANAGER "+e);}

            if(instance==null){
                diagnosticInt("tb371fc_native_night_receiver_present",null);
                diagnosticInt("tb371fc_native_ai_history_count",null);
                diagnosticInt("tb371fc_native_ai_enabled",null);
            }else{
                try{
                    Field receiver=manager.getDeclaredField("i");receiver.setAccessible(true);
                    diagnosticInt("tb371fc_native_night_receiver_present",receiver.get(null)==null?0:1);
                }catch(Exception e){
                    System.err.println("TB371FC NATIVE_DIAG_RECEIVER "+e);
                    diagnosticInt("tb371fc_native_night_receiver_present",null);
                }
                try{
                    Method aiHistory=manager.getDeclaredMethod("O");aiHistory.setAccessible(true);
                    Method rawHistory=Class.forName("com.miui.powercenter.h").getDeclaredMethod("Z");rawHistory.setAccessible(true);
                    String raw=(String)rawHistory.invoke(null);
                    Object records=aiHistory.invoke(instance);
                    Integer count=null;
                    if(records instanceof java.util.List){
                        int size=((java.util.List<?>)records).size();
                        if(size>0||raw!=null&&raw.trim().equals("[]"))count=size;
                    }
                    diagnosticInt("tb371fc_native_ai_history_count",count);
                }catch(Exception e){
                    System.err.println("TB371FC NATIVE_DIAG_AI_HISTORY "+e);
                    diagnosticInt("tb371fc_native_ai_history_count",null);
                }
                try{
                    Field ai=manager.getDeclaredField("p");ai.setAccessible(true);
                    Object value=ai.get(null);
                    diagnosticInt("tb371fc_native_ai_enabled",value instanceof Boolean?((Boolean)value?1:0):null);
                }catch(Exception e){
                    System.err.println("TB371FC NATIVE_DIAG_AI "+e);
                    diagnosticInt("tb371fc_native_ai_enabled",null);
                }
            }

            try{
                Class<?> persistence=Class.forName("D2.e");
                Method legacy=persistence.getDeclaredMethod("m",String.class,java.util.ArrayList.class);legacy.setAccessible(true);
                Object records=legacy.invoke(null,"key_night_charge_record",new java.util.ArrayList<>());
                diagnosticInt("tb371fc_native_night_history_count",records instanceof java.util.List?((java.util.List<?>)records).size():null);
            }catch(Exception e){
                System.err.println("TB371FC NATIVE_DIAG_HISTORY "+e);
                diagnosticInt("tb371fc_native_night_history_count",null);
            }
            try{
                String capacity=(String)Class.forName("miui.util.FeatureParser").getMethod("getString",String.class).invoke(null,"battery_capacity_typ");
                diagnosticInt("tb371fc_native_capacity_typ",capacity==null||capacity.length()==0?null:Integer.parseInt(capacity));
            }catch(Exception e){
                System.err.println("TB371FC NATIVE_DIAG_CAPACITY "+e);
                diagnosticInt("tb371fc_native_capacity_typ",null);
            }
            try{putTime("tb371fc_native_diag_time");}
            catch(Exception e){System.err.println("TB371FC NATIVE_DIAG_TIME "+e);}
        }catch(Exception e){
            System.err.println("TB371FC NATIVE_DIAG "+e);
        }finally{
            synchronized(ChargeUi.class){providerDiagnosticRunning=false;}
        }
    }
    public static void providerResult(String method){
        if(!method.equals("getThisTimeNoProtect")&&!method.equals("setThisTimeNoProtectWorking")&&!method.equals("getSupportNightProtect"))return;
        try{
            Class<?> type=Class.forName("com.miui.powercenter.charge.protect.s");
            Method singleton=type.getDeclaredMethod("l");singleton.setAccessible(true);Object instance=singleton.invoke(null);
            Method state=type.getDeclaredMethod("n");state.setAccessible(true);int value=(Integer)state.invoke(instance);
            if(value!=lastOnce){put("tb371fc_once_full_state",value);putTime("tb371fc_once_full_time");lastOnce=value;}
        }catch(Exception e){System.err.println("TB371FC NATIVE_ONCE "+e);}
        if(method.equals(PROVIDER_INIT_METHOD)&&managerReady)refreshProviderDiagnostics(false);
    }
    public static void modeChanged(Object unused,int mode) {
        try {put("tb371fc_smart_command",0x10);put("tb371fc_smart_common_limit",0);put("tb371fc_smart_navigation_limit",0);put("tb371fc_smart_night",0);sequence();}
        catch(Exception e){System.err.println("TB371FC MODE_RESET "+e);}
        requestProviderInit();
        scheduleStatusRefresh();
    }
    static Object call(Object target,String method,Class<?>[] types,Object...args) throws Exception {
        return target.getClass().getMethod(method,types).invoke(target,args);
    }
    static Object find(Object fragment,String key) throws Exception {
        return call(fragment,"findPreference",new Class<?>[]{CharSequence.class},key);
    }
    static void title(Object p,String value) throws Exception {
        call(p,"setTitle",new Class<?>[]{CharSequence.class},value);
    }
    static void summary(Object p,String value) throws Exception {
        call(p,"setSummary",new Class<?>[]{CharSequence.class},value);
    }
    static boolean zh(){return Locale.getDefault().getLanguage().equals("zh");}
    static String node(String name)throws Exception {
        return new String(java.nio.file.Files.readAllBytes(java.nio.file.Paths.get("/sys/class/power_supply/battery/"+name)),java.nio.charset.StandardCharsets.UTF_8).trim();
    }
    public static String batteryStatus(){
        try{
            String soc=node("capacity"),status=node("status"),label;
            int mode=get(MODE,0);
            boolean held=get("tb371fc_charge_hold",0)==1;
            if(status.equals("Charging"))label=zh()?"充电中":"Charging";
            else if(status.equals("Full"))label=zh()?"已充满":"Full";
            else if(status.equals("Discharging"))label=zh()?"放电中":"Discharging";
            else if(held&&mode==3)label=zh()?"旁路供电中":"Bypass power";
            else if(held&&mode==2)label=zh()?"健康保护中":"Battery health protection";
            else if(held&&mode==1)label=zh()?"智能保护中":"Smart charging protection";
            else label=zh()?"已暂停充电":"Charging paused";
            return soc+"% | "+label;
        }catch(Exception e){return zh()?"电量状态暂不可用":"Battery status unavailable";}
    }
    public static void attach(Object fragment) {
        attachedFragment=new java.lang.ref.WeakReference<>(fragment);
        requestProviderInit();
        try {
            Object normal=find(fragment,"cb_charge_normal");
            Object smart=find(fragment,"cb_intellect_charge_protect");
            Object health=find(fragment,"cb_always_charge_protect");
            Object group=find(fragment,"category_features_battery_protect");
            if(smart==null||health==null||group==null)throw new IllegalStateException("Expected R8 preferences missing");
            if(normal!=null)call(normal,"setOrder",new Class<?>[]{int.class},0);
            call(smart,"setOrder",new Class<?>[]{int.class},1);
            call(health,"setOrder",new Class<?>[]{int.class},2);
            title(smart,zh()?"智能充电保护":"Smart charging protection");
            summary(smart,zh()?"根据使用及充电习惯智能调控，避免长时间饱充":"Adjust charging to usage and charging habits");
            Object bypass=find(fragment,"tb371fc_bypass_power");
            Class<?> preference=Class.forName("androidx.preference.Preference");
            if(bypass==null){
                Class<?> smartType=smart.getClass();
                Object smartContext=call(smart,"getContext",new Class<?>[0]);
                bypass=smartType.getConstructor(Class.forName("android.content.Context")).newInstance(smartContext);
                int layout=(Integer)call(smart,"getLayoutResource",new Class<?>[0]);
                int widgetLayout=(Integer)call(smart,"getWidgetLayoutResource",new Class<?>[0]);
                call(bypass,"setLayoutResource",new Class<?>[]{int.class},layout);
                call(bypass,"setWidgetLayoutResource",new Class<?>[]{int.class},widgetLayout);
                call(bypass,"setKey",new Class<?>[]{String.class},"tb371fc_bypass_power");
                call(bypass,"setPersistent",new Class<?>[]{boolean.class},false);
                call(bypass,"setOrder",new Class<?>[]{int.class},3);
                Class<?> listener=Class.forName("androidx.preference.Preference$c");
                Object callback=Proxy.newProxyInstance(listener.getClassLoader(),new Class<?>[]{listener},(proxy,method,args)->{
                    if(method.getName().equals("equals"))return proxy==args[0];
                    if(method.getName().equals("hashCode"))return System.identityHashCode(proxy);
                    if(method.getName().equals("toString"))return "TB371FCBypassPreference";
                    if(args!=null&&args.length>1&&Boolean.FALSE.equals(args[1]))return true;
                    try{modeChanged(null,3);put(MODE,3);scheduleBypassSelection();return true;}
                    catch(Exception e){e.printStackTrace();return false;}
                });
                call(bypass,"setOnPreferenceChangeListener",new Class<?>[]{listener},callback);
                call(group,"addPreference",new Class<?>[]{preference},bypass);
            }
            title(bypass,zh()?"旁路供电":"Bypass power");
            summary(bypass,zh()?"亮屏使用时由外部供电；息屏或低电量恢复充电，电源不足时自动退出":"External power while in use; charging resumes when asleep or battery is low");
            int mode=get(MODE,0);
            Object selected=mode==3?bypass:mode==2?health:mode==1?smart:normal;
            applySelection(group,selected);
        }catch(Exception e){System.err.println("TB371FC UI_ATTACH "+e);e.printStackTrace();}
    }
}
