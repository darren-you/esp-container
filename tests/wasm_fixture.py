"""Minimal valid Classic ABI Wasm bytes shared by host/device package tests."""

HEADER = b"\0asm\x01\0\0\0"
TYPES = (b"\x05\x60\x00\x01\x7f\x60\x02\x7f\x7f\x01\x7f"
         b"\x60\x00\x01\x7e\x60\x02\x7f\x7f\x01\x7e"
         b"\x60\x01\x7e\x01\x7f")


def leb(number: int) -> bytes:
    output = bytearray()
    while True:
        octet = number & 0x7F
        number >>= 7
        output.append(octet | (0x80 if number else 0))
        if number == 0:
            return bytes(output)


def section(kind: int, content: bytes) -> bytes:
    return bytes((kind,)) + leb(len(content)) + content


def name(value: str) -> bytes:
    encoded = value.encode("ascii")
    return leb(len(encoded)) + encoded


def module(imports: tuple[str, ...] = (), *, event_type: int = 1,
           memory_flags: int = 1, memory_pages: int = 1, memory_max: int = 1,
           duplicate_export: bool = False, code_count: int = 3,
           extra: bytes = b"") -> bytes:
    type_by_name = {"monotonic_ms": 2, "log": 1,
                    "timer_start": 3, "timer_cancel": 4}
    imported = (leb(len(imports)) + b"".join(
        name("econtainer") + name(field) + b"\0" + leb(type_by_name.get(field, 2))
        for field in imports)) if imports else b""
    function_types = b"\x03\0" + leb(event_type) + b"\0"
    memory = b"\x01" + leb(memory_flags) + leb(memory_pages) + (
        leb(memory_max) if memory_flags & 1 else b"")
    exported = [("econtainer_init", 0, len(imports)),
                ("econtainer_on_event", 0, len(imports) + 1),
                ("econtainer_stop", 0, len(imports) + 2),
                ("memory", 2, 0)]
    if duplicate_export:
        exported[1] = ("econtainer_init", 0, len(imports) + 1)
    exports = leb(len(exported)) + b"".join(
        name(field) + bytes((kind,)) + leb(index) for field, kind, index in exported)
    body = b"\0\x41\0\x0b"
    code = leb(code_count) + (leb(len(body)) + body) * code_count
    return (HEADER + section(1, TYPES) +
            (section(2, imported) if imports else b"") +
            section(3, function_types) + section(5, memory) +
            section(7, exports) + section(10, code) + extra)
