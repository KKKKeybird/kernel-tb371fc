public final class ChargePolicyTest {
 static int checks;
 static void check(boolean v,String label){checks++;if(!v)throw new AssertionError(label);}
 public static void main(String[] args){
  ChargePolicy p=new ChargePolicy();
  check(!p.stop(0,79,true,true,80,true),"Native target below normal");
  check(p.stop(0,80,true,true,80,true),"Normal follows native target");
  check(p.stop(0,79,true,true,80,true),"Normal native 80/78 hysteresis");
  check(!p.stop(0,78,true,true,80,true),"Normal releases at native 78");

  check(!p.stop(2,74,true,true,75,true),"Health follows native variable target");
  check(p.stop(2,75,true,true,75,true),"Health stops at native 75");
  check(p.stop(2,74,true,true,75,true),"Health native target hysteresis");
  check(!p.stop(2,73,true,true,75,true),"Health releases at native 73");
  check(!p.stop(2,75,true,true,0,true),"Native clear releases health");
  check(p.stop(2,85,true,true,85,true),"Native target acquired later");
  check(!p.stop(2,85,true,true,90,true),"Native target raises release");

  check(!p.stop(1,79,true,true,80,true),"Smart below native target");
  check(p.stop(1,80,true,true,80,true),"Smart honors native target");
  check(!p.stop(1,80,true,true,0,true),"Smart honors native clear");
  check(p.stop(1,90,true,true,90,true),"Smart variable target 90");

  check(p.stop(2,82,true,false,80,true),"Mode 2 native hold");
  check(p.stop(1,82,true,false,80,true),"Switch to mode 1 keeps native hold");
  check(p.stop(0,82,true,false,80,true),"Switch to mode 0 keeps native hold");
  check(!p.stop(3,82,true,false,0,true),"Switch to bypass releases native hold");

  check(p.stop(3,68,true,true,0,true),"Bypass while awake");
  check(!p.stop(3,68,true,false,0,true),"Bypass releases while asleep");
  check(p.stop(3,68,true,true,0,true),"Bypass resumes after wake");
  check(!p.stop(3,68,true,true,0,false),"Bypass low supply fallback");
  check(p.stop(3,68,true,true,0,true),"Bypass retries recovered supply");

  check(p.stop(3,25,true,true,0,true),"Bypass arms at 25");
  check(p.stop(3,21,true,true,0,true),"Bypass keeps 20/25 hysteresis");
  check(!p.stop(3,20,true,true,0,true),"Bypass releases at low battery");
  check(!p.stop(3,24,true,true,0,true),"Bypass waits until 25 after low battery");
  check(p.stop(3,25,true,true,0,true),"Bypass re-arms at 25");

  check(p.stop(3,90,true,true,80,true),"Native protection coexists with bypass");
  check(p.stop(3,90,true,false,80,false),"Native protection overrides bypass conditions");
  check(p.stop(3,55,true,false,55,false),"Stricter native target survives bypass release");
  check(!p.stop(3,55,true,false,0,false),"Native clear leaves bypass released");
  check(p.stop(3,80,true,true,80,false),"Native protection ignores supply backoff");

  check(!p.stop(3,68,false,true,0,true),"Disconnect releases bypass");
  check(!p.stop(2,85,false,true,80,true),"Disconnect releases native protection");
  check(!p.stop(1,-1,true,true,80,true),"Invalid SOC releases");
  check(!p.stop(4,68,true,true,0,true),"Unknown mode releases");
  check(!p.stop(2,68,true,true,19,true),"Invalid native target releases");
  System.out.println("PASS "+checks+" native-target and bypass transitions");
 }
}
