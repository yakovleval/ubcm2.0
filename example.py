from pathlib import Path


def node(data, next0=0, next1=0):
    value = data << 48 | next0 << 32 | next1 << 16
    return value.to_bytes(8, "big")


def rn(nodes):
    return (b"UBCMRN01" + len(nodes).to_bytes(8, "big")
            + (0).to_bytes(8, "big") + b"".join(nodes))


def standard_nodes():
    nodes = [node(3, 2*i + 1, 2*i + 2) for i in range(15)]
    nodes += [node(11) for _ in range(16)]
    nodes[24] = node(9)   # 1001: RETURN
    nodes[26] = node(11)  # 1011: EXIT
    return nodes


bits = "1001000000000000101011"
Path("program.ubc").write_bytes(
    int(bits.ljust(24, "0"), 2).to_bytes(3, "big")
)
Path("1.rn").write_bytes(rn(standard_nodes()))
Path("2.rn").write_bytes(rn([
    node(3, 8, 1), node(3, 2, 8), node(3, 3, 8),
    node(3, 8, 4), node(3, 5, 8), node(9, 6, 6),
    node(3, 8, 7), node(3, 8, 8), node(11),
]))
