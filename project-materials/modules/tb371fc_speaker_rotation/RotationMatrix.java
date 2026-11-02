import java.util.Arrays;

final class RotationMatrix {
 static final int GAIN=16384;
 // Fixed selector1 slots measured at camera-up: LL, LU, RL, RU.
 private static final int[][] SLOT={{-1,1},{-1,-1},{1,1},{1,-1}};
 static int[] target(int rotation,int cameraUpRotation){
  if(rotation<0||rotation>3)throw new IllegalArgumentException("rotation");
  int turns=(rotation-cameraUpRotation+4)%4;
  int[] matrix=new int[16];
  for(int row=0;row<4;row++){
   int x=SLOT[row][0],y=SLOT[row][1];
   for(int n=0;n<turns;n++){int nx=y;y=-x;x=nx;}
   int col=(y>0?2:0)+(x>0?1:0);
   matrix[row*4+col]=GAIN;
  }
  return matrix;
 }
 static void validate(int[] matrix){
  if(matrix.length!=16)throw new IllegalArgumentException("matrix size");
  for(int row=0;row<4;row++){
   int sum=0;
   for(int col=0;col<4;col++){
    int v=matrix[row*4+col];
    if(v<0||v>GAIN)throw new IllegalArgumentException("matrix weight");
    sum+=v;
   }
   if(sum!=GAIN)throw new IllegalArgumentException("matrix row gain");
  }
 }
 static int[] blend(int[] start,int[] target,int step,int steps){
  validate(start);validate(target);
  if(step<0||step>steps||steps<=0)throw new IllegalArgumentException("step");
  int[] result=new int[16];
  // Cumulative rounding preserves the exact row sum without gain boosts.
  for(int row=0;row<4;row++){
   long total=0,previous=0;
   for(int col=0;col<4;col++){
    int i=row*4+col;
    total+=(long)start[i]*(steps-step)+(long)target[i]*step;
    long rounded=(total+steps/2)/steps;
    result[i]=(int)(rounded-previous);previous=rounded;
   }
  }
  validate(result);return result;
 }
 static int[] parse(String snapshot){
  String marker="SpkRot 0 Matrix:";
  int pos=snapshot.indexOf(marker);
  if(pos<0)throw new IllegalArgumentException("kernel matrix control missing");
  String value=snapshot.substring(pos+marker.length()).split("[\\r\\n(]",2)[0].trim();
  String[] numbers=value.split("\\s+");
  if(numbers.length!=16)throw new IllegalArgumentException("matrix readback");
  int[] result=new int[16];
  for(int i=0;i<16;i++)result[i]=Integer.parseInt(numbers[i]);
  validate(result);return result;
 }
}
