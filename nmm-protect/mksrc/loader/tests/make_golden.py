"""Generate reproducible test-only key/nonce vector, never a production payload."""
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pack import seal

plain = bytes(i & 255 for i in range(320))
payload, _ = seal(plain, bytes(range(16)), sys.argv[1] if len(sys.argv) > 1 else 'java',
                  key=bytes(range(32)), nonce=bytes(range(12)))
out = Path(__file__).parent
(out / 'envelope.golden').write_bytes(payload)
(out / 'envelope-golden.json').write_text(json.dumps({
    'purpose': 'test only; never reuse these key/nonce values for protection',
    'key_hex': bytes(range(32)).hex(), 'nonce_hex': bytes(range(12)).hex(),
    'build_id_hex': bytes(range(16)).hex(), 'decoded_bytes': len(plain),
    'decoded_sha256': hashlib.sha256(plain).hexdigest(),
    'payload_bytes': len(payload), 'payload_sha256': hashlib.sha256(payload).hexdigest()
}, indent=2) + '\n')
