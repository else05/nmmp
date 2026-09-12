import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import pack
elf = pack.Elf(Path(sys.argv[1]).read_bytes())
imports = pack.imports_for(elf, sys.argv[3], sys.argv[4])
Path(sys.argv[2]).write_bytes(pack.split(elf, imports))
