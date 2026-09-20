import importlib.util
import tempfile
from pathlib import Path


spec = importlib.util.spec_from_file_location(
    "inventory_game_memory", Path(__file__).resolve().parents[1] / "tools/inventory_game_memory.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def test_inventory():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        path = root / "src/camera/Hook.cpp"
        path.parent.mkdir(parents=True)
        path.write_bytes(b"// ignored uintptr_t\nvoid* saved;\nVirtualProtect(saved, 8, 4, &old);\n"
                         b"*reinterpret_cast<int*>(image + 0x123456) = 1;\n// old encoding \xb2\n")
        rows = list(module.inventory(root))
        assert [row[2] for row in rows] == [2, 3, 4]
        assert "pointer-access" in rows[0][3]
        assert "detour-or-write" in rows[1][3]
        assert "image-or-offset" in rows[2][3]


if __name__ == "__main__":
    test_inventory()
