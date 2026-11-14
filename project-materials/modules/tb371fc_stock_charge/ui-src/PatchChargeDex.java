import java.util.*;
import java.nio.file.*;
import org.jf.dexlib2.*;
import org.jf.dexlib2.iface.*;
import org.jf.dexlib2.iface.instruction.*;
import org.jf.dexlib2.builder.*;
import org.jf.dexlib2.builder.instruction.*;
import org.jf.dexlib2.immutable.*;
import org.jf.dexlib2.immutable.reference.*;
import org.jf.dexlib2.dexbacked.*;
import org.jf.dexlib2.writer.pool.DexPool;

public final class PatchChargeDex {
 static final String FRAGMENT="Lcom/miui/powercenter/nightcharge/ChargeProtectFragment;";
 static final String UTILS="Lcom/miui/powercenter/charge/protect/e;";
 static final String HELPER="LChargeUi;";
 static ImmutableMethodReference ref(String n,List<String> params,String result){return new ImmutableMethodReference(HELPER,n,params,result);}
 static BuilderInstruction invoke(String name,List<String> params,String result,int...regs){
  int[] r=new int[5];System.arraycopy(regs,0,r,0,regs.length);
  return new BuilderInstruction35c(Opcode.INVOKE_STATIC,regs.length,r[0],r[1],r[2],r[3],r[4],ref(name,params,result));
 }
 static BuilderInstruction invokeOne(String name,int register){
  return register<16?invoke(name,Collections.singletonList("Ljava/lang/String;"),"V",register):new BuilderInstruction3rc(Opcode.INVOKE_STATIC_RANGE,register,1,ref(name,Collections.singletonList("Ljava/lang/String;"),"V"));
 }
 static Method modified(Method m,MethodImplementation impl){return new ImmutableMethod(m.getDefiningClass(),m.getName(),m.getParameters(),m.getReturnType(),m.getAccessFlags(),m.getAnnotations(),m.getHiddenApiRestrictions(),impl);}
 static Method wrapper(Method m,String target,List<String> params,String result){
  MutableMethodImplementation b=new MutableMethodImplementation(Math.max(1,params.size()));
  b.addInstruction(invoke(target,params,result,params.isEmpty()?new int[]{}:new int[]{0}));
  if(result.equals("V"))b.addInstruction(new BuilderInstruction10x(Opcode.RETURN_VOID));
  else {boolean object=result.startsWith("L");b.addInstruction(new BuilderInstruction11x(object?Opcode.MOVE_RESULT_OBJECT:Opcode.MOVE_RESULT,0));b.addInstruction(new BuilderInstruction11x(object?Opcode.RETURN_OBJECT:Opcode.RETURN,0));}
  return modified(m,b);
 }
 public static void main(String[] args)throws Exception{
  Opcodes op=Opcodes.forApi(30);
  DexBackedDexFile source=new DexBackedDexFile(op,Files.readAllBytes(Paths.get(args[0])));
  DexBackedDexFile helper=new DexBackedDexFile(op,Files.readAllBytes(Paths.get(args[1])));
  List<ClassDef> classes=new ArrayList<>();Set<String> changed=new TreeSet<>();
  for(ClassDef c:source.getClasses()){
   List<Method> methods=new ArrayList<>();boolean edited=false;
   for(Method m:c.getMethods()){
    Method n=m;
    if(c.getType().equals(FRAGMENT)&&m.getName().equals("onCreatePreferences")){
     MutableMethodImplementation b=new MutableMethodImplementation(m.getImplementation());
     int self=b.getRegisterCount()-3;
     for(int i=b.getInstructions().size()-1;i>=0;i--)if(b.getInstructions().get(i).getOpcode()==Opcode.RETURN_VOID)b.addInstruction(i,invoke("attach",Collections.singletonList("Ljava/lang/Object;"),"V",self));
     n=modified(m,b);
    }
    if(c.getType().equals(FRAGMENT)&&m.getName().equals("getBatteryStatusNow")&&m.getParameterTypes().isEmpty())n=wrapper(m,"batteryStatus",Collections.emptyList(),"Ljava/lang/String;");
    if(c.getType().equals("Lcom/miui/powercenter/provider/PowerCenterProvider;")&&m.getName().equals("call")&&m.getParameterTypes().size()==3){
     MutableMethodImplementation b=new MutableMethodImplementation(m.getImplementation());int methodReg=b.getRegisterCount()-3;
     for(int i=b.getInstructions().size()-1;i>=0;i--)if(b.getInstructions().get(i).getOpcode()==Opcode.RETURN_OBJECT)b.addInstruction(i,invokeOne("providerResult",methodReg));
     b.addInstruction(0,invoke("ensureChargingManager",Collections.emptyList(),"V"));n=modified(m,b);
    }
    if(c.getType().equals(UTILS)){
     if(m.getName().equals("g")&&m.getParameterTypes().toString().equals("[Landroid/content/Context;]")&&m.getReturnType().equals("I"))n=wrapper(m,"systemMode",Collections.singletonList("Ljava/lang/Object;"),"I");
     if(m.getName().equals("t")&&m.getReturnType().equals("Z"))n=wrapper(m,"setSmartCommand",Collections.singletonList("Ljava/lang/String;"),"Z");
     if(m.getName().equals("f")&&m.getReturnType().equals("Ljava/lang/String;"))n=wrapper(m,"getSmartState",Collections.emptyList(),"Ljava/lang/String;");
     if(m.getName().equals("r")&&m.getReturnType().equals("Ljava/lang/Boolean;"))n=wrapper(m,"setNightState",Collections.singletonList("I"),"Ljava/lang/Boolean;");
     if(m.getName().equals("s")&&m.getParameterTypes().toString().equals("[Landroid/content/Context;, I]")){
      MutableMethodImplementation b=new MutableMethodImplementation(m.getImplementation());int first=b.getRegisterCount()-2;
      b.addInstruction(0,invoke("modeChanged",Arrays.asList("Ljava/lang/Object;","I"),"V",first,first+1));
      n=modified(m,b);
     }
    }
    if(n!=m){edited=true;changed.add(c.getType()+"->"+m.getName());}methods.add(n);
   }
   classes.add(edited?new ImmutableClassDef(c.getType(),c.getAccessFlags(),c.getSuperclass(),c.getInterfaces(),c.getSourceFile(),c.getAnnotations(),c.getFields(),methods):c);
  }
  for(ClassDef c:helper.getClasses())classes.add(c);
  if(changed.size()!=8)throw new IllegalStateException("Unexpected patch scope "+changed);
  DexPool.writeTo(args[2],new ImmutableDexFile(op,classes));
  System.out.println("Patched methods: "+changed);
 }
}
