import java.nio.file.*;import java.util.*;import java.security.*;import java.lang.reflect.*;
import org.jf.dexlib2.*;import org.jf.dexlib2.dexbacked.*;import org.jf.dexlib2.iface.*;
import org.jf.dexlib2.iface.instruction.*;import org.jf.dexlib2.iface.reference.Reference;
import org.jf.dexlib2.util.ReferenceUtil;
public final class AuditChargeDex {
 static String key(org.jf.dexlib2.iface.Method m){return m.getDefiningClass()+"->"+m.getName()+m.getParameterTypes()+m.getReturnType();}
 static String value(Object o){
  if(o instanceof Reference)return ReferenceUtil.getReferenceString((Reference)o);
  if(o instanceof Iterable){StringBuilder b=new StringBuilder();for(Object x:(Iterable<?>)o){if(x instanceof SwitchElement)b.append(((SwitchElement)x).getKey()).append(':').append(((SwitchElement)x).getOffset());else b.append(value(x));b.append(';');}return b.toString();}
  return String.valueOf(o);
 }
 static byte[] body(org.jf.dexlib2.iface.Method m)throws Exception{
  MessageDigest d=MessageDigest.getInstance("SHA-256");
  d.update(Integer.toString(m.getAccessFlags()).getBytes("UTF-8"));MethodImplementation b=m.getImplementation();if(b==null)return d.digest();
  d.update(Integer.toString(b.getRegisterCount()).getBytes("UTF-8"));
  for(Instruction i:b.getInstructions()){
   TreeMap<String,String> getters=new TreeMap<>();
   for(java.lang.reflect.Method g:i.getClass().getMethods())if(g.getParameterCount()==0&&g.getName().startsWith("get")&&!g.getName().equals("getClass"))getters.put(g.getName(),value(g.invoke(i)));
   d.update(getters.toString().getBytes("UTF-8"));
  }
  for(TryBlock<? extends ExceptionHandler> t:b.getTryBlocks()){
   d.update((t.getStartCodeAddress()+":"+t.getCodeUnitCount()).getBytes("UTF-8"));
   for(ExceptionHandler h:t.getExceptionHandlers())d.update((h.getExceptionType()+":"+h.getHandlerCodeAddress()).getBytes("UTF-8"));
  }
  return d.digest();
 }
 public static void main(String[] args)throws Exception{
  Map<String,byte[]> old=new HashMap<>();
  DexBackedDexFile a=new DexBackedDexFile(Opcodes.forApi(30),Files.readAllBytes(Paths.get(args[0])));
  for(ClassDef c:a.getClasses())for(org.jf.dexlib2.iface.Method m:c.getMethods())old.put(key(m),body(m));
  DexBackedDexFile b=new DexBackedDexFile(Opcodes.forApi(30),Files.readAllBytes(Paths.get(args[1])));
  List<String> changes=new ArrayList<>(),added=new ArrayList<>();int unchanged=0;
  for(ClassDef c:b.getClasses())for(org.jf.dexlib2.iface.Method m:c.getMethods()){
   String key=key(m);byte[] before=old.remove(key);
   if(before==null)added.add(key);else if(Arrays.equals(before,body(m)))unchanged++;else changes.add(key);
  }
  if(!old.isEmpty())throw new AssertionError("Removed methods "+old.keySet());
  if(changes.size()!=8)throw new AssertionError("Unexpected changed methods "+changes);
  for(String k:changes)if(!k.startsWith("Lcom/miui/powercenter/provider/PowerCenterProvider;->call")&&!k.startsWith("Lcom/miui/powercenter/charge/protect/e;->")&&!k.startsWith("Lcom/miui/powercenter/nightcharge/ChargeProtectFragment;->onCreatePreferences")&&!k.startsWith("Lcom/miui/powercenter/nightcharge/ChargeProtectFragment;->getBatteryStatusNow"))throw new AssertionError(k);
  for(String k:added)if(!k.startsWith("LChargeUi"))throw new AssertionError("Unexpected added method "+k);
  System.out.println("PASS unchanged original method bodies="+unchanged+" modified="+changes+" added helper methods="+added.size());
 }
}
