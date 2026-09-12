package bench;
public final class HarnessSelfCheck {
 public static void main(String[] args) {
  BenchMain.verify();
  for(String name:new String[]{"arithmetic","branch","shortCall","largeShort","jniObject","arrayPayload","exception","recursive","multiThread"})
   for(int n:new int[]{0,1,255,257,1000})
    if(BenchMain.run(name,n)!=BenchMain.expected(name,n))throw new AssertionError(name+"/"+n);
  System.out.println("PASS: 45 independent driver totals including four-thread workloads");
 }
}