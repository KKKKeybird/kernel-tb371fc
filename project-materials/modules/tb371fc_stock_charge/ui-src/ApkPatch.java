import java.io.*;
import java.nio.file.*;
import java.nio.file.attribute.*;
import java.util.*;
import java.util.zip.*;
/** Streaming, aligned ZIP rewrite; replaces only classes3.dex. */
public final class ApkPatch {
 static final class Count extends OutputStream {
  final OutputStream out;long count;
  Count(OutputStream out){this.out=out;}
  public void write(int b)throws IOException{out.write(b);count++;}
  public void write(byte[] b,int o,int n)throws IOException{out.write(b,o,n);count+=n;}
  public void close()throws IOException{out.close();}
  public void flush()throws IOException{out.flush();}
 }
 static void transfer(InputStream in,OutputStream out)throws IOException{byte[] b=new byte[65536];int n;while((n=in.read(b))!=-1)out.write(b,0,n);}
 static final OutputStream DISCARD=new OutputStream(){
  public void write(int b){}
  public void write(byte[] b,int offset,int length){}
 };
 static void verify(Path zip,List<String> expected)throws IOException{
  if(expected.indexOf("classes3.dex")!=expected.lastIndexOf("classes3.dex"))throw new IOException("Duplicate target member");
  if(expected.indexOf("classes3.dex")<0)throw new IOException("Missing target");
  try(ZipFile check=new ZipFile(zip.toFile())){
   if(check.size()!=expected.size())throw new IOException("Member count "+check.size()+" expected "+expected.size());
   int index=0;
   for(Enumeration<? extends ZipEntry> entries=check.entries();entries.hasMoreElements();){
    ZipEntry entry=entries.nextElement();
    if(index>=expected.size()||!expected.get(index++).equals(entry.getName()))throw new IOException("Member order mismatch");
    try(InputStream stream=check.getInputStream(entry)){transfer(stream,DISCARD);}
   }
   if(index!=expected.size())throw new IOException("Member count "+index+" expected "+expected.size());
  }
 }
 public static void main(String[] args)throws Exception {
  Path target=Paths.get(args[2]);Path absolute=target.toAbsolutePath().normalize();Path directory=absolute.getParent();
  if(directory==null)throw new IOException("Output has no parent directory");
  Path temp=Files.createTempFile(directory,"."+absolute.getFileName()+".", ".tmp");boolean committed=false;
  try{
   List<String> members=new ArrayList<>();
   try(ZipFile input=new ZipFile(args[0]);Count counted=new Count(Files.newOutputStream(temp));ZipOutputStream out=new ZipOutputStream(counted)){
    int matches=0;
    for(Enumeration<? extends ZipEntry> entries=input.entries();entries.hasMoreElements();){
     ZipEntry old=entries.nextElement();boolean replace=old.getName().equals("classes3.dex");
     ZipEntry next=new ZipEntry(old.getName());next.setTime(old.getTime());next.setMethod(old.getMethod());
     if(old.getComment()!=null)next.setComment(old.getComment());
     if(old.getMethod()==ZipEntry.STORED){
      if(replace)throw new IOException("Unexpected stored target DEX");
      next.setSize(old.getSize());next.setCompressedSize(old.getSize());next.setCrc(old.getCrc());
      int alignment=old.getName().endsWith(".so")?16384:4;
      long header=counted.count+30+old.getName().getBytes("UTF-8").length;
      int padding=(int)((alignment-header%alignment)%alignment);
      if(padding>0){
       if(padding<4)padding+=alignment;
       byte[] extra=new byte[padding];extra[0]=0x35;extra[1]=(byte)0xd9;
       extra[2]=(byte)(padding-4);extra[3]=(byte)((padding-4)>>>8);next.setExtra(extra);
      }
     }
     out.putNextEntry(next);
     try(InputStream stream=replace?new FileInputStream(args[1]):input.getInputStream(old)){transfer(stream,out);}
     out.closeEntry();members.add(next.getName());if(replace)matches++;
    }
    if(matches!=1)throw new IOException("Target member count "+matches);
   }
   verify(temp,members);
   Files.setPosixFilePermissions(temp,PosixFilePermissions.fromString("rw-r--r--"));
   Files.move(temp,target,StandardCopyOption.ATOMIC_MOVE,StandardCopyOption.REPLACE_EXISTING);committed=true;
  }finally{if(!committed)Files.deleteIfExists(temp);}
 }
}
