// Ghidra headless: analyzeHeadless <project> OpenBGI -process ac2_ML.exe -noanalysis
// -scriptPath analysis/scripts -postScript DumpInstructions.java <output-file> <VA>...
// Dumps decoded x86 instructions for return-value and calling-convention research.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import java.io.PrintWriter;

public class DumpInstructions extends GhidraScript {
 public void run() throws Exception {
  String[] args=getScriptArgs(); if(args.length<2) throw new IllegalArgumentException("output and address required");
  try(PrintWriter out=new PrintWriter(args[0],"UTF-8")) {
   for(int i=1;i<args.length;i++){
    Address a=toAddr(args[i]);Function f=getFunctionContaining(a);
    if(f==null){out.println("NO FUNCTION "+args[i]);continue;}
    out.println("\n=== "+f.getName()+" "+f.getEntryPoint()+" ===");
    InstructionIterator it=currentProgram.getListing().getInstructions(f.getBody(),true);
    while(it.hasNext()){Instruction x=it.next();out.println(x.getAddress()+"  "+x.toString());}
   }
  }
 }
}
