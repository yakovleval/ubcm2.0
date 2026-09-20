from pathlib import Path


OUTPUT_DIR = Path(__file__).parent


def variable_size(value):
    if value < 0 or value >= 1 << 64:
        raise ValueError("variable size must fit uint64")
    byte_count = max(1, (value.bit_length() + 7) // 8)
    return format(byte_count - 1, "03b") + format(value, f"0{byte_count * 8}b")


def immediate(value):
    bit_count = max(1, value.bit_length())
    return "00" + variable_size(bit_count) + format(value, f"0{bit_count}b")


def register_selector(reg_num):
    return "11" + format(reg_num, "05b")


def direct_address(reg_num, offset=0):
    return "10" + register_selector(reg_num) + variable_size(offset)


def direct_source(reg_num, offset, size_bits):
    return direct_address(reg_num, offset) + variable_size(size_bits)


def resize(reg_num, size_bits):
    return "1100" + register_selector(reg_num) + immediate(size_bits)


def get_size(reg_num, destination):
    return "1101" + register_selector(reg_num) + destination


def copy(source, destination):
    return "0101" + source + destination


def return_immediate(value):
    return "1001" + immediate(value)


def procedure_with_subroutine(build_main, subroutine):
    offset = 0
    for _ in range(8):
        main = build_main(offset)
        new_offset = len(main)
        if new_offset == offset:
            return main + subroutine
        offset = new_offset
    raise RuntimeError("procedure offset did not converge")


def make_node(data, next0, next1):
    value = 0
    value |= (data & 0x7FFF) << 48
    value |= (next0 & 0xFFFF) << 32
    value |= (next1 & 0xFFFF) << 16
    return value


def command_leaf_index(command_bits):
    index = 0
    for bit in command_bits:
        index = 2 * index + (1 if bit == "0" else 2)
    return index


def add_tree(nodes, base, commands):
    for index in range(15):
        nodes[base + index] = make_node(
            0x03,
            base + 2 * index + 1,
            base + 2 * index + 2,
        )
    for index in range(15, 31):
        nodes[base + index] = make_node(0x0B, 0, 0)
    for command_bits, command_code in commands.items():
        index = command_leaf_index(command_bits)
        nodes[base + index] = make_node(command_code, 0, 0)


def add_exact_path(nodes, base, bits, command_code, sink):
    for index, bit in enumerate(bits):
        next_node = base + index + 1
        if bit == "0":
            nodes[base + index] = make_node(0x03, next_node, sink)
        else:
            nodes[base + index] = make_node(0x03, sink, next_node)
    nodes[base + len(bits)] = make_node(command_code, 0, 0)
    nodes[sink] = make_node(0x0B, 0, 0)


def build_network(*command_sets):
    nodes = [0] * (31 * len(command_sets))
    for tree_index, commands in enumerate(command_sets):
        add_tree(nodes, tree_index * 31, commands)
    return b"".join(node.to_bytes(8, "big") for node in nodes)


def pack_bits(bits):
    padded = bits + "0" * (-len(bits) % 8)
    return bytes(
        int(padded[index:index + 8], 2)
        for index in range(0, len(padded), 8)
    )


def write_case(name, program, network):
    (OUTPUT_DIR / f"{name}.ubc").write_bytes(pack_bits(program))
    (OUTPUT_DIR / f"{name}.rn").write_bytes(network)
    print(f"{name}: {len(program)} program bits, {len(network) // 8} RS nodes")


def generate_call_new_procedure_0110():
    program = procedure_with_subroutine(
        lambda offset: "0110" + direct_address(1, offset) + "1011",
        return_immediate(11) + "1011",
    )
    network = build_network({"0110": 0x06, "1001": 0x09, "1011": 0x0B})
    write_case("call_new_procedure_0110", program, network)


def generate_call_new_network_0111():
    entry_offset = 0
    for _ in range(8):
        body = (
            "0111"
            + direct_address(1, entry_offset)
            + return_immediate(17)
            + "1011"
            + "1011"
        )
        new_offset = len(body)
        if new_offset == entry_offset:
            break
        entry_offset = new_offset
    else:
        raise RuntimeError("entry-value offset did not converge")

    program = body + format(31, "064b")
    network = build_network(
        {"0111": 0x07, "1011": 0x0B},
        {"1001": 0x09},
    )
    write_case("call_new_network_0111", program, network)


def generate_call_new_procedure_and_network_1000():
    procedure = (
        return_immediate(17)
        + "1011"
        + immediate(23)
        + "1011"
    )
    procedure_offset = 0
    for _ in range(8):
        bootstrap = (
            "0110"
            + direct_address(1, procedure_offset)
            + "1000"
            + direct_address(1, procedure_offset)
            + direct_address(2, 31)
            + "1011"
        )
        new_offset = len(bootstrap)
        if new_offset == procedure_offset:
            break
        procedure_offset = new_offset
    else:
        raise RuntimeError("procedure offset did not converge")

    program = bootstrap + procedure
    nodes = [0] * 59
    add_tree(
        nodes,
        0,
        {"0110": 0x06, "1000": 0x08, "1001": 0x09, "1011": 0x0B},
    )
    alternative_name = return_immediate(17) + "1011"
    add_exact_path(nodes, 31, alternative_name, 0x09, 58)
    network = b"".join(node.to_bytes(8, "big") for node in nodes)
    write_case("call_new_procedure_and_network_1000", program, network)


def generate_resize_register_1100():
    program = (
        resize(20, 13)
        + resize(20, 21)
        + resize(20, 5)
        + resize(20, 0)
        + resize(21, 9)
        + "1011"
    )
    network = build_network({"1011": 0x0B, "1100": 0x0C})
    write_case("resize_register_1100", program, network)


def generate_get_register_size_1101():
    program = (
        resize(20, 13)
        + resize(21, 128)
        + get_size(20, direct_address(21, 0))
        + get_size(22, direct_address(21, 64))
        + "1011"
    )
    network = build_network({"1011": 0x0B, "1100": 0x0C, "1101": 0x0D})
    write_case("get_register_size_1101", program, network)


def generate_return_result_1001():
    program = return_immediate(42) + "1011"
    network = build_network({"1001": 0x09, "1011": 0x0B})
    write_case("return_result_1001", program, network)


def generate_copy_value_0101():
    value = int("1011010010110", 2)
    program = (
        resize(20, 20)
        + copy(immediate(value), direct_address(20, 0))
        + copy(direct_source(20, 0, 13), direct_address(20, 7))
        + "1011"
    )
    network = build_network({"0101": 0x05, "1011": 0x0B, "1100": 0x0C})
    write_case("copy_value_0101", program, network)


def main():
    generate_call_new_procedure_0110()
    generate_call_new_network_0111()
    generate_call_new_procedure_and_network_1000()
    generate_resize_register_1100()
    generate_get_register_size_1101()
    generate_return_result_1001()
    generate_copy_value_0101()


if __name__ == "__main__":
    main()
