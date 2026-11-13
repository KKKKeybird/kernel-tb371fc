final class RotationPolicy {
 // Surface rotation is opposite the previous gravity quadrant.
 // This tablet's camera-up landscape is Surface ROTATION_90, selector 0.
 static int displaySelector(int rotation){
  if(rotation<0||rotation>3)throw new IllegalArgumentException();
  return (5-rotation)%4;
 }
 static int halRotation(int selector){return ((4-selector)%4)*90;}
 static boolean allowed(int mode,String profile){
  return mode==0&&("music".equals(profile)||"unavailable".equals(profile));
 }
}
