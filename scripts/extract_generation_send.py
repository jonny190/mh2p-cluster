"""Extract exact production send functions for the Windows adapter fixture.
CC BY-NC-SA 4.0; see LICENSE.md. Never imports or executes firmware.
"""
import sys
from pathlib import Path
source=Path(sys.argv[1]).read_text(encoding="utf-8")
start=source.index("static int32_t m2_native_send(")
end=source.index("static int32_t m2_queue_locked(",start)
Path(sys.argv[2]).write_text(source[start:end],encoding="utf-8")
