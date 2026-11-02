final class CoverPlan {
 final boolean closed,blockWake,requestSaver,pendingCharge,sleep,wake;
 CoverPlan(int lid,boolean initial,boolean charging,boolean guardReady){
  if(lid!=0&&lid!=1)throw new IllegalArgumentException("invalid lid");
  closed=lid==0;blockWake=closed&&guardReady;pendingCharge=closed&&charging;
  requestSaver=closed&&!charging;sleep=closed;wake=!closed&&!initial;
 }
}
