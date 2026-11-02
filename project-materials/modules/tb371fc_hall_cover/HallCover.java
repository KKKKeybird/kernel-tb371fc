import java.lang.reflect.*;
import java.util.*;
import java.util.concurrent.*;
import java.io.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import android.os.UEventObserver;
public class HallCover {
 static final HallDebouncer filter=new HallDebouncer();
 static Object handler,wakeLock,powerWakeLock,power,manager,listener,context;
 static Method postDelayed,removeCallbacks;
 static Path dir;
 static Integer lid;
 static boolean charging=true,guardReady,stopping,probe;
 static final String TOKEN="TB371FC_COVER";
 static long now(){return System.nanoTime()/1000000L;}
 static String command(String...cmd)throws Exception{
  Process p=new ProcessBuilder(cmd).redirectErrorStream(true).start();
  if(!p.waitFor(2,TimeUnit.SECONDS)){p.destroyForcibly();throw new IOException("command timeout "+cmd[0]);}
  String result=new String(p.getInputStream().readAllBytes(),StandardCharsets.UTF_8).trim();
  if(p.exitValue()!=0)throw new IOException("command failed "+Arrays.toString(cmd)+" "+result);
  return result;
 }
 static void prop(String key,boolean value)throws Exception{command("/system/bin/setprop",key,value?"1":"0");}
 static void releaseWake(){try{if((Boolean)wakeLock.getClass().getMethod("isHeld").invoke(wakeLock))wakeLock.getClass().getMethod("release").invoke(wakeLock);}catch(Exception e){System.err.println(e);}}
 static void status()throws Exception{
  if(lid==null)return;
  CoverPlan p=new CoverPlan(lid,false,charging,guardReady);
  boolean saver=(Boolean)power.getClass().getMethod("isPowerSaveMode").invoke(power);
  boolean awake=(Boolean)power.getClass().getMethod("isInteractive").invoke(power);
  String text="LID="+(p.closed?"CLOSED":"OPEN")+" guard="+(p.blockWake?"ACTIVE":"OFF")+" saver="+saver+" charging="+charging+" saver_pending_charge="+p.pendingCharge+" interactive="+awake;
  System.out.println(text);System.out.flush();
  Path temp=dir.resolve("status.tmp");Files.write(temp,(text+"\n").getBytes(StandardCharsets.UTF_8));Files.move(temp,dir.resolve("status.txt"),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
 }
 static final Runnable verify=()->{try{status();}catch(Exception e){System.err.println("VERIFY_ERROR "+e);}};
 static void saveOnly()throws Exception{
  if(lid==null||probe)return;
  CoverPlan p=new CoverPlan(lid,false,charging,guardReady);
  command("/system/bin/cmd","power","set-mode",p.requestSaver?"1":"0");
  removeCallbacks.invoke(handler,verify);postDelayed.invoke(handler,verify,500L);
 }
 static void policy(int value,boolean initial)throws Exception{
  charging=hardwareCharging();
  lid=value;CoverPlan p=new CoverPlan(value,initial,charging,guardReady);
  if(probe){status();return;}
  // Clear/open the wake gate before emitting the intentional open-cover wake.
  prop("sys.tb371fc.cover_closed",p.closed);
  prop("sys.tb371fc.cover_block_wake",p.blockWake);
  if(p.sleep)command("/system/bin/input","keyevent","KEYCODE_SLEEP");
  command("/system/bin/cmd","power","suppress-ambient-display",TOKEN,p.closed?"true":"false");
  saveOnly();
  if(p.wake)command("/system/bin/input","keyevent","KEYCODE_WAKEUP");
  System.out.println("POLICY lid="+value+" initial="+initial+" debounce_ms="+HallDebouncer.DELAY_MS);System.out.flush();
  status();
 }
 static final Runnable settle=()->{
  Integer value=filter.commit(now());
  try{if(value!=null)policy(value,false);}catch(Exception e){System.err.println("POLICY_ERROR "+e);}
  finally{releaseWake();}
 };
 static void apply(float v)throws Exception{
  if(stopping || (v!=0f&&v!=1f))return;
  if(lid==null){filter.event((int)v,now());policy((int)v,true);return;}
  if(!filter.event((int)v,now()))return;
  removeCallbacks.invoke(handler,settle);
  if(filter.due()<0){releaseWake();System.out.println("BOUNCE_CANCELLED");return;}
  wakeLock.getClass().getMethod("acquire",long.class).invoke(wakeLock,HallDebouncer.DELAY_MS+1000);
  postDelayed.invoke(handler,settle,Math.max(0,filter.due()-now()));
 }
 static PowerEvents powerEvents;
 static Boolean supplied(String name,String field){
  try(InputStream in=new FileInputStream("/sys/class/power_supply/"+name+"/"+field)){byte[] b=new byte[32];int n=in.read(b);return n>0?"1".equals(new String(b,0,n,StandardCharsets.US_ASCII).trim()):null;}catch(Exception e){return null;}
 }
 static boolean hardwareCharging(){
  Boolean usb=supplied("usb","present"),dc=supplied("dc","present"),wireless=supplied("wireless","online");
  if(Boolean.TRUE.equals(usb)||Boolean.TRUE.equals(dc)||Boolean.TRUE.equals(wireless))return true;
  if(usb!=null&&dc!=null)return false;
  return charging;
 }
 static int powerAttempts;
 static void releasePowerWake(){try{if(powerWakeLock!=null&&(Boolean)powerWakeLock.getClass().getMethod("isHeld").invoke(powerWakeLock))powerWakeLock.getClass().getMethod("release").invoke(powerWakeLock);}catch(Exception e){System.err.println(e);}}
 static final Runnable powerRetry=new Runnable(){public void run(){
  try{
   if(stopping||probe||lid==null){releasePowerWake();return;}
   charging=hardwareCharging();CoverPlan p=new CoverPlan(lid,false,charging,guardReady);
   boolean actual=(Boolean)power.getClass().getMethod("isPowerSaveMode").invoke(power);
   if(actual!=p.requestSaver)saveOnly();
   status();
   if(actual==p.requestSaver||++powerAttempts>=3){releasePowerWake();return;}
   postDelayed.invoke(handler,this,750L);
  }catch(Exception e){System.err.println("POWER_RETRY_ERROR "+e);try{if(++powerAttempts<3)postDelayed.invoke(handler,this,750L);else releasePowerWake();}catch(Exception ignored){releasePowerWake();}}
 }};
 static final Runnable powerUpdate=()->{
  boolean next=hardwareCharging();
  if(next!=charging){
   charging=next;System.out.println("POWER_UEVENT charging="+charging);System.out.flush();
   try{
    if(!probe){powerWakeLock.getClass().getMethod("acquire",long.class).invoke(powerWakeLock,3000L);powerAttempts=0;removeCallbacks.invoke(handler,powerRetry);postDelayed.invoke(handler,powerRetry,0L);}
   }catch(Exception e){System.err.println("POWER_ERROR "+e);releasePowerWake();}
  }
 };
 static final class PowerEvents extends UEventObserver {
  public void onUEvent(UEventObserver.UEvent event){
   if(stopping)return;
   String name=event.get("POWER_SUPPLY_NAME");
   if(!Arrays.asList("usb","dc","wireless","pc_port").contains(name))return;
   if(probe){System.out.println("UEVENT_PROBE name="+name+" charging="+hardwareCharging());System.out.flush();}
   try{removeCallbacks.invoke(handler,powerUpdate);postDelayed.invoke(handler,powerUpdate,150L);}catch(Exception e){System.err.println("UEVENT_ERROR "+e);}
  }
 }
 static void cleanup(){
  stopping=true;
  if(powerEvents!=null)powerEvents.stopObserving();
  if(probe)return;
  try{prop("sys.tb371fc.cover_block_wake",false);prop("sys.tb371fc.cover_closed",false);}catch(Exception e){System.err.println(e);}
  try{command("/system/bin/cmd","power","suppress-ambient-display",TOKEN,"false");command("/system/bin/cmd","power","set-mode","0");}catch(Exception e){System.err.println(e);}
  releaseWake();releasePowerWake();
 }
 public static void main(String[] args)throws Exception{
  dir=Paths.get(args[0]);probe=args.length>1&&"--probe".equals(args[1]);
  Class<?> looper=Class.forName("android.os.Looper");looper.getMethod("prepare").invoke(null);
  Class<?> at=Class.forName("android.app.ActivityThread");Method sm=at.getDeclaredMethod("systemMain");sm.setAccessible(true);Object thread=sm.invoke(null);
  Method gc=at.getDeclaredMethod("getSystemContext");gc.setAccessible(true);context=gc.invoke(thread);
  Class<?> cc=Class.forName("android.content.Context");
  Class<?> hc=Class.forName("android.os.Handler");handler=hc.getConstructor().newInstance();postDelayed=hc.getMethod("postDelayed",Runnable.class,long.class);removeCallbacks=hc.getMethod("removeCallbacks",Runnable.class);
  power=cc.getMethod("getSystemService",String.class).invoke(context,"power");wakeLock=power.getClass().getMethod("newWakeLock",int.class,String.class).invoke(power,1,"TB371FC:HallDebounce");wakeLock.getClass().getMethod("setReferenceCounted",boolean.class).invoke(wakeLock,false);
  powerWakeLock=power.getClass().getMethod("newWakeLock",int.class,String.class).invoke(power,1,"TB371FC:CoverPowerTransition");powerWakeLock.getClass().getMethod("setReferenceCounted",boolean.class).invoke(powerWakeLock,false);
  guardReady="1".equals(command("/system/bin/getprop","sys.tb371fc.cover_guard_ready"));
  charging=hardwareCharging();
  powerEvents=new PowerEvents();powerEvents.startObserving("SUBSYSTEM=power_supply");
  Runtime.getRuntime().addShutdownHook(new Thread(HallCover::cleanup));
  manager=cc.getMethod("getSystemService",String.class).invoke(context,"sensor");Class<?> mgr=Class.forName("android.hardware.SensorManager"),sensor=Class.forName("android.hardware.Sensor"),lc=Class.forName("android.hardware.SensorEventListener");Object target=null;
  for(Object s:(List<?>)mgr.getMethod("getSensorList",int.class).invoke(manager,-1))if((Integer)sensor.getMethod("getType").invoke(s)==33171002){if(target==null||(Boolean)sensor.getMethod("isWakeUpSensor").invoke(s))target=s;}
  if(target==null)throw new IllegalStateException("hall sensor unavailable");
  listener=Proxy.newProxyInstance(lc.getClassLoader(),new Class<?>[]{lc},(proxy,m,a)->{
   switch(m.getName()){
    case "hashCode":return System.identityHashCode(proxy);
    case "equals":return proxy==a[0];
    case "toString":return "TB371FC HallCover policy";
    case "onSensorChanged":float[] values=(float[])a[0].getClass().getField("values").get(a[0]);System.out.println("HALL "+System.currentTimeMillis()+" "+Arrays.toString(values));System.out.flush();apply(values[0]);return null;
    default:return null;
   }
  });
  boolean ok=(Boolean)mgr.getMethod("registerListener",lc,sensor,int.class).invoke(manager,listener,target,3);System.out.println("REGISTERED="+ok+" guard_ready="+guardReady+" charging="+charging);System.out.flush();if(!ok)throw new IOException("sensor registration rejected");
  if(probe)postDelayed.invoke(handler,(Runnable)()->System.exit(0),6000L);
  looper.getMethod("loop").invoke(null);
 }
}
