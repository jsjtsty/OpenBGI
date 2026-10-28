// Exports decompiled C, function index, strings, imports, symbols of the current program.
// Usage: -postScript ExportAll.java <outDir>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.address.*;
import java.io.*;
import java.util.*;

public class ExportAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String dir = getScriptArgs().length > 0 ? getScriptArgs()[0] : ".";
        String pn = currentProgram.getName();
        int dot = pn.lastIndexOf('.');
        String base = dot > 0 ? pn.substring(0, dot) : pn;
        new File(dir).mkdirs();

        try (PrintWriter w = new PrintWriter(new File(dir, base + ".imports.txt"), "UTF-8")) {
            for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols())
                w.println(s.getParentNamespace().getName() + "!" + s.getName());
        }
        try (PrintWriter w = new PrintWriter(new File(dir, base + ".strings.txt"), "UTF-8")) {
            for (Data d : currentProgram.getListing().getDefinedData(true)) {
                if (!d.hasStringValue()) continue;
                StringDataInstance si = StringDataInstance.getStringDataInstance(d);
                w.println(d.getAddress() + "\t" + si.getStringValue().replace('\n', ' ').replace('\r', ' '));
            }
        }
        try (PrintWriter w = new PrintWriter(new File(dir, base + ".symbols.txt"), "UTF-8")) {
            for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(false))
                if (s.getSource() != SourceType.DEFAULT)
                    w.println(s.getAddress() + "\t" + s.getSymbolType() + "\t" + s.getName(true));
        }

        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        FunctionManager fm = currentProgram.getFunctionManager();
        int total = fm.getFunctionCount(), n = 0;
        try (PrintWriter c = new PrintWriter(new File(dir, base + ".c"), "UTF-8");
             PrintWriter idx = new PrintWriter(new File(dir, base + ".functions.tsv"), "UTF-8")) {
            idx.println("addr\tsize\tname\tsignature");
            for (Function f : fm.getFunctions(true)) {
                monitor.checkCancelled();
                n++;
                if (n % 500 == 0) println("decompiled " + n + "/" + total);
                idx.println(f.getEntryPoint() + "\t" + f.getBody().getNumAddresses() + "\t" + f.getName(true) + "\t" + f.getPrototypeString(false, false));
                if (f.isThunk() || f.isExternal()) continue;
                DecompileResults r = ifc.decompileFunction(f, 60, monitor);
                c.println("// ==== " + f.getEntryPoint() + " " + f.getName(true) + " ====");
                if (r.decompileCompleted()) c.println(r.getDecompiledFunction().getC());
                else c.println("// decompile failed: " + r.getErrorMessage());
            }
        }
        println("done: " + n + " functions");
    }
}
