import java.io.*;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.Arrays;
import java.util.concurrent.*;
import java.util.regex.*;

public class SpeakerRotation {
 static File dir;
 static Object displayManager,displayListener,power,audioService,audioManager,modeListener;
 static Method getDisplay,getRotation,getState,interactive,phoneMode,setParams;
 static Class<?> displayListenerClass;
 static ScheduledExecutorService worker;
 static ScheduledFuture<?> pending;
 static boolean probe;
 static volatile int seenRotation=-1,seenState=-1;
 static volatile long generation;
 static Object resources,resolver;
 static Class<?> resourcesClass;
 static Method resourceId,resourceInteger;
 static final Object gate=new Object();
 static String lastStatus="";
 static int cameraUpRotation=1,rampMs=240;
 static int[] curMatrix;
 static long writeEstimateNs=60_000_000L;

 static String command(String...cmd)throws Exception {
  Process p=new ProcessBuilder(cmd).redirectErrorStream(true).start();
  if(!p.waitFor(3,TimeUnit.SECONDS)){p.destroyForcibly();throw new IOException("command timeout");}
  String s=new String(p.getInputStream().readAllBytes(),StandardCharsets.UTF_8).trim();
  if(p.exitValue()!=0)throw new IOException("command failed: "+s);
  return s;
 }
 static String profile(String s){Matcher m=Pattern.compile(">([^ ]+)").matcher(s);return m.find()?m.group(1):"unknown";}
 static int selector(String s){Matcher m=Pattern.compile("TFA9894_CHAN_SELECTOR: (-?\\d+)").matcher(s);return m.find()?Integer.parseInt(m.group(1)):-99;}
 static void trace(String s){
  try{
   String line=new java.text.SimpleDateFormat("HH:mm:ss.SSS").format(new java.util.Date())+" "+s+"\n";
   Files.write(new File(dir,"status.log").toPath(),line.getBytes(StandardCharsets.UTF_8),StandardOpenOption.CREATE,StandardOpenOption.APPEND);
  }catch(Exception ignored){}
 }
 static void status(String s)throws Exception {
  if(s.equals(lastStatus))return;
  System.out.println(s);System.out.flush();
  Path tmp=new File(dir,"status.tmp").toPath();
  Files.write(tmp,(s+"\n").getBytes(StandardCharsets.UTF_8));
  Files.move(tmp,new File(dir,"status.txt").toPath(),StandardCopyOption.REPLACE_EXISTING,StandardCopyOption.ATOMIC_MOVE);
  lastStatus=s;
 }
 static int setting(String name,int fallback){
  try{return Integer.parseInt(new String(Files.readAllBytes(new File(dir,name).toPath()),StandardCharsets.UTF_8).trim());}
  catch(Exception ignored){return fallback;}
 }
 static void loadTuning(){
  cameraUpRotation=Math.floorMod(setting("camera_up_rotation",1),4);
  rampMs=Math.max(0,Math.min(1000,setting("ramp_ms",240)));
 }
 static void request(String reason,long delayMs){
  synchronized(gate){
   if(pending!=null)pending.cancel(false);
   final long eventNs=System.nanoTime(),ticket=++generation;
   pending=worker.schedule(()->{
    try{apply(reason,eventNs,ticket);}
    catch(Exception e){try{status("ERROR "+e);}catch(Exception ignored){}}
   },delayMs,TimeUnit.MILLISECONDS);
  }
 }
 static void displayChanged()throws Exception {
  Object d=getDisplay.invoke(displayManager,0);
  if(d==null)return;
  int r=(Integer)getRotation.invoke(d),s=(Integer)getState.invoke(d);
  if(r==seenRotation&&s==seenState)return;
  seenRotation=r;seenState=s;
  System.out.println("DISPLAY_EVENT rotation="+r+" state="+s);System.out.flush();
  request("display",0);
 }
 static boolean ready(int rotation,long ticket)throws Exception {
  if(ticket!=generation)return false;
  if(new File(dir,"disable").exists()||new File(dir,"remove").exists())System.exit(0);
  Object d=getDisplay.invoke(displayManager,0);
  return d!=null&&(Integer)getRotation.invoke(d)==rotation&&
   (Boolean)interactive.invoke(power)&&(Integer)getState.invoke(d)==2&&
   (Integer)phoneMode.invoke(audioService)==0;
 }
 static void writeMatrix(int[] matrix)throws Exception {
  RotationMatrix.validate(matrix);
  String[] args=new String[17];
  args[0]=new File(dir,"matrix-write").getAbsolutePath();
  for(int i=0;i<16;i++)args[i+1]=Integer.toString(matrix[i]);
  command(args);
 }
 static void apply(String reason,long eventNs,long ticket)throws Exception {
  if(ticket!=generation)return;
  Object d=getDisplay.invoke(displayManager,0);
  if(d==null)return;
  int r=(Integer)getRotation.invoke(d);
  if(probe){status("PROBE display_rotation="+r+" matrix=4x4 no_audio_writes");return;}
  if(!ready(r,ticket)){status("IDLE screen_or_mode_guard no_audio_writes");return;}
  String snapshot=command("/system/bin/sh","-c",
   "/system/bin/tinymix 'TFA Profile' && /system/bin/tinymix TFA9894_CHAN_SELECTOR && /system/bin/tinymix 'SpkRot 0 Matrix'");
  String p=profile(snapshot);
  if(!RotationPolicy.allowed((Integer)phoneMode.invoke(audioService),p)){
   status("PAUSED profile="+p+" keep_call_protection");return;
  }
  curMatrix=RotationMatrix.parse(snapshot);
  // Set the fixed amplifier geometry only once after boot/profile setup.
  // Rotations themselves never write the selector or HAL gsensor_rotation.
  if(selector(snapshot)!=1){
   if(!ready(r,ticket))return;
   command("/system/bin/tinymix","TFA9894_CHAN_SELECTOR","1");
   if(selector(command("/system/bin/tinymix","TFA9894_CHAN_SELECTOR"))!=1)
    throw new IOException("fixed selector setup failed");
  }
  int[] startMatrix=curMatrix.clone(),target=RotationMatrix.target(r,cameraUpRotation);
  if(Arrays.equals(startMatrix,target)){status("SYNC display_rotation="+r+" matrix=4x4");return;}
  long startNs=System.nanoTime();
  int steps=rampMs==0?1:Math.max(2,Math.min(8,(int)(rampMs*1_000_000L/Math.max(30_000_000L,writeEstimateNs))));
  for(int step=1;step<=steps;step++){
   long targetNs=startNs+(long)rampMs*1_000_000L*step/steps-writeEstimateNs;
   while(System.nanoTime()<targetNs){
    if(!ready(r,ticket))return;
    long waitNs=Math.min(targetNs-System.nanoTime(),20_000_000L);
    if(waitNs>0)TimeUnit.NANOSECONDS.sleep(waitNs);
   }
   if(!ready(r,ticket))return;
   int[] next=RotationMatrix.blend(startMatrix,target,step,steps);
   long before=System.nanoTime();
   try{writeMatrix(next);}catch(Exception failure){
    // The kernel reports failed DSP writes and attempts rollback. Re-read
    // the committed state; never advance interpolation on a failed write.
    try{curMatrix=RotationMatrix.parse(command("/system/bin/tinymix","SpkRot 0 Matrix"));}catch(Exception ignored){}
    throw failure;
   }
   curMatrix=next;
   long elapsed=System.nanoTime()-before;
   writeEstimateNs=(writeEstimateNs*3+elapsed)/4;
   trace("MATRIX_STEP rotation="+r+" step="+step+"/"+steps+" write_ms="+elapsed/1_000_000L);
  }
  status("ROUTE display_rotation="+r+" matrix=4x4 source="+reason+
   " switch_start_ms="+(startNs-eventNs)/1_000_000L+
   " event_to_done_ms="+(System.nanoTime()-eventNs)/1_000_000L);
 }
 public static void main(String[] args)throws Exception {
  dir=new File(args[0]);probe=args.length>1&&args[1].equals("--probe");
  loadTuning();
  Class<?> looper=Class.forName("android.os.Looper");looper.getMethod("prepare").invoke(null);
  Object thread=Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
  Method gc=thread.getClass().getDeclaredMethod("getSystemContext");gc.setAccessible(true);Object ctx=gc.invoke(thread);
  Method service=Class.forName("android.content.Context").getMethod("getSystemService",String.class);
  displayManager=service.invoke(ctx,"display");power=service.invoke(ctx,"power");audioManager=service.invoke(ctx,"audio");
  resources=Class.forName("android.content.Context").getMethod("getResources").invoke(ctx);
  resolver=Class.forName("android.content.Context").getMethod("getContentResolver").invoke(ctx);
  resourcesClass=Class.forName("android.content.res.Resources");resourceId=resourcesClass.getMethod("getIdentifier",String.class,String.class,String.class);resourceInteger=resourcesClass.getMethod("getInteger",int.class);
  Class<?> dm=Class.forName("android.hardware.display.DisplayManager"),display=Class.forName("android.view.Display");
  getDisplay=dm.getMethod("getDisplay",int.class);getRotation=display.getMethod("getRotation");getState=display.getMethod("getState");
  interactive=Class.forName("android.os.PowerManager").getMethod("isInteractive");
  Object binder=Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"audio");
  audioService=Class.forName("android.media.IAudioService$Stub").getMethod("asInterface",Class.forName("android.os.IBinder")).invoke(null,binder);
  phoneMode=Class.forName("android.media.IAudioService").getMethod("getMode");
  setParams=Class.forName("android.media.AudioSystem").getMethod("setParameters",String.class);
  worker=Executors.newSingleThreadScheduledExecutor();
  displayListenerClass=Class.forName("android.hardware.display.DisplayManager$DisplayListener");
  displayListener=Proxy.newProxyInstance(displayListenerClass.getClassLoader(),new Class<?>[]{displayListenerClass},(proxy,m,a)->{
   switch(m.getName()){
    case "hashCode":return System.identityHashCode(proxy);
    case "equals":return proxy==a[0];
    case "toString":return "TB371FC display rotation";
    case "onDisplayChanged":case "onDisplayAdded":if((Integer)a[0]==0)displayChanged();return null;
    default:return null;
   }
  });
  Class<?> handler=Class.forName("android.os.Handler");Object h=handler.getConstructor(looper).newInstance(looper.getMethod("myLooper").invoke(null));
  dm.getMethod("registerDisplayListener",displayListenerClass,handler).invoke(displayManager,displayListener,h);
  // Refuse to run on an old kernel instead of silently using a stereo-only path.
  if(!new File(dir,"matrix-write").canExecute())throw new IOException("atomic matrix writer missing");
  curMatrix=RotationMatrix.parse(command("/system/bin/tinymix","SpkRot 0 Matrix"));
  if(!probe&&(Integer)phoneMode.invoke(audioService)==0){
   String initial=command("/system/bin/sh","-c",
    "/system/bin/tinymix 'TFA Profile' && /system/bin/tinymix TFA9894_CHAN_SELECTOR");
   if(RotationPolicy.allowed(0,profile(initial))&&selector(initial)!=1){
    command("/system/bin/tinymix","TFA9894_CHAN_SELECTOR","1");
    if(selector(command("/system/bin/tinymix","TFA9894_CHAN_SELECTOR"))!=1)
     throw new IOException("initial fixed selector setup failed");
   }
  }
  Class<?> ml=Class.forName("android.media.AudioManager$OnModeChangedListener");
  modeListener=Proxy.newProxyInstance(ml.getClassLoader(),new Class<?>[]{ml},(proxy,m,a)->{
   switch(m.getName()){
    case "hashCode":return System.identityHashCode(proxy);
    case "equals":return proxy==a[0];
    case "toString":return "TB371FC audio mode guard";
    case "onModeChanged":request("audio_mode",100);return null;
    default:return null;
   }
  });
  audioManager.getClass().getMethod("addOnModeChangedListener",Executor.class,ml).invoke(audioManager,worker,modeListener);
  Runtime.getRuntime().addShutdownHook(new Thread(()->{
   worker.shutdownNow();
   try{dm.getMethod("unregisterDisplayListener",displayListenerClass).invoke(displayManager,displayListener);}catch(Exception ignored){}
   try{audioManager.getClass().getMethod("removeOnModeChangedListener",ml).invoke(audioManager,modeListener);}catch(Exception ignored){}
  }));
  System.out.println("REGISTERED display_listener=true mode_listener=true periodic_poll=false accelerometer=false matrix=4x4 fade_ms="+rampMs+"");System.out.flush();
  displayChanged();
  if(probe)worker.schedule(()->System.exit(0),6,TimeUnit.SECONDS);
  looper.getMethod("loop").invoke(null);
 }
}
