package android.os; public class Binder { public Binder(){} protected boolean onTransact(int code,Parcel data,Parcel reply,int flags)throws RemoteException{return false;} }
