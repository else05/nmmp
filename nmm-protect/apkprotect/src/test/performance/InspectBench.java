import com.android.tools.smali.dexlib2.dexbacked.DexBackedDexFile;
import com.android.tools.smali.dexlib2.iface.ClassDef;
import com.android.tools.smali.dexlib2.iface.Method;
import com.android.tools.smali.dexlib2.iface.instruction.Instruction;
import com.google.gson.GsonBuilder;
import java.io.BufferedInputStream;
import java.io.FileInputStream;
import java.util.Map;
import java.util.TreeMap;

public final class InspectBench {
    public static void main(String[] args) throws Exception {
        Map<String, Object> report = new TreeMap<>();
        try (BufferedInputStream stream = new BufferedInputStream(new FileInputStream(args[0]))) {
            for (ClassDef cls : DexBackedDexFile.fromInputStream(null, stream).getClasses()) {
                for (Method method : cls.getMethods()) {
                    if (method.getName().startsWith("<")) continue;
                    Map<String, Object> row = new TreeMap<>();
                    Map<String, Integer> opcodes = new TreeMap<>();
                    int units = 0;
                    for (Instruction instruction : method.getImplementation().getInstructions()) {
                        units += instruction.getCodeUnits();
                        String opcode = instruction.getOpcode().name;
                        opcodes.put(opcode, opcodes.getOrDefault(opcode, 0) + 1);
                    }
                    row.put("codeBytes", units * 2);
                    row.put("registers", method.getImplementation().getRegisterCount());
                    row.put("tryBlocks", method.getImplementation().getTryBlocks().size());
                    row.put("opcodes", opcodes);
                    report.put(method.getName(), row);
                }
            }
        }
        if (report.size() != 8) throw new AssertionError("expected eight implementation methods");
        System.out.println(new GsonBuilder().setPrettyPrinting().create().toJson(report));
    }
}
