from pathlib import Path


OUTPUT_DIR = Path(__file__).parent
REG_PROCEDURE = "00"
REG_LOCAL = "01"
REG_SUPERLOCAL = "10"
REG_GLOBAL = "11"


def variable_size(value):
    if value < 0 or value >= 1 << 64:
        raise ValueError("variable size must fit uint64")
    byte_count = max(1, (value.bit_length() + 7) // 8)
    return format(byte_count - 1, "03b") + format(value, f"0{byte_count * 8}b")


def immediate(value):
    bit_count = max(1, value.bit_length())
    return "00" + variable_size(bit_count) + format(value, f"0{bit_count}b")


def immediate_sized(value, bit_count):
    if value < 0 or value >= 1 << bit_count:
        raise ValueError("immediate does not fit requested size")
    return "00" + variable_size(bit_count) + format(value, f"0{bit_count}b")


def integer(value):
    bit_count = max(1, value.bit_length())
    return variable_size(bit_count) + format(value, f"0{bit_count}b")


def read_prefix(depth):
    return "0000" + integer(depth)


def write_prefix(depth):
    return "0001" + integer(depth)


def conditional_prefix(reference):
    return "0010" + reference


def register_selector(reg_num, reg_class=REG_GLOBAL):
    return reg_class + format(reg_num, "05b")


def direct_address(reg_num, offset=0, reg_class=REG_GLOBAL):
    return (
        "10"
        + register_selector(reg_num, reg_class)
        + variable_size(offset)
    )


def direct_source(reg_num, offset, size_bits, reg_class=REG_GLOBAL):
    return (
        direct_address(reg_num, offset, reg_class)
        + variable_size(size_bits)
    )


def indirect_address(reg_num, offset=0, reg_class=REG_GLOBAL):
    return (
        "01"
        + register_selector(reg_num, reg_class)
        + variable_size(offset)
    )


def indirect_source(reg_num, offset, size_bits, reg_class=REG_GLOBAL):
    return (
        indirect_address(reg_num, offset, reg_class)
        + variable_size(size_bits)
    )


def foreign_address(depth_address, working_address):
    return "11" + depth_address + working_address


def foreign_source(depth_address, working_address, size_bits):
    return (
        foreign_address(depth_address, working_address)
        + variable_size(size_bits)
    )


def resize(reg_num, size_bits, reg_class=REG_GLOBAL):
    return (
        "1100"
        + register_selector(reg_num, reg_class)
        + immediate(size_bits)
    )


def get_size(reg_num, destination, reg_class=REG_GLOBAL):
    return (
        "1101"
        + register_selector(reg_num, reg_class)
        + destination
    )


def jump(position):
    return "1010" + immediate(position)


def copy(source, destination):
    return "0101" + source + destination


def compute(opcode, source1, source2, destination):
    return (
        "0100"
        + format(opcode, "05b")
        + source1
        + source2
        + destination
    )


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


LOCAL_RESOLVER_BASE = 128
SUPERLOCAL_RESOLVER_BASE = 191


def make_node(data, next0, next1, resolver=0, node_type=0):
    value = (node_type & 1) << 63
    value |= (data & 0x7FFF) << 48
    value |= (next0 & 0xFFFF) << 32
    value |= (next1 & 0xFFFF) << 16
    value |= resolver & 0xFFFF
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
    for command_bits, command in commands.items():
        if isinstance(command, tuple):
            if len(command) == 2:
                command_code, resolver = command
                node_type = 0
            else:
                command_code, resolver, node_type = command
        else:
            command_code, resolver = command, 0
            node_type = 0
        index = command_leaf_index(command_bits)
        nodes[base + index] = make_node(
            command_code, 0, 0, resolver, node_type
        )


def add_identity_resolver(nodes, base):
    required_size = base + 63
    if len(nodes) < required_size:
        nodes.extend([0] * (required_size - len(nodes)))
    for index in range(31):
        nodes[base + index] = make_node(
            0x03,
            base + 2 * index + 1,
            base + 2 * index + 2,
        )
    for name in range(32):
        nodes[base + 31 + name] = make_node(name, 0, 0)


def add_exact_path(nodes, base, bits, command_code, sink):
    for index, bit in enumerate(bits):
        next_node = base + index + 1
        if bit == "0":
            nodes[base + index] = make_node(0x03, next_node, sink)
        else:
            nodes[base + index] = make_node(0x03, sink, next_node)
    nodes[base + len(bits)] = make_node(command_code, 0, 0)
    nodes[sink] = make_node(0x0B, 0, 0)


def serialize_network(nodes, initial_superlocals):
    result = bytearray(b"UBCMRN01")
    result += len(nodes).to_bytes(8, "big")
    result += len(initial_superlocals).to_bytes(8, "big")
    for node in nodes:
        result += node.to_bytes(8, "big")
    for node_index, logical_name, bits in initial_superlocals:
        result += node_index.to_bytes(8, "big")
        result += logical_name.to_bytes(1, "big")
        result += len(bits).to_bytes(8, "big")
        result += pack_bits(bits)
    return bytes(result)


def build_network(
    *command_sets, superlocal=False, initial_superlocals=()
):
    nodes = [0] * (31 * len(command_sets))
    for tree_index, commands in enumerate(command_sets):
        add_tree(nodes, tree_index * 31, commands)
    add_identity_resolver(nodes, LOCAL_RESOLVER_BASE)
    if superlocal:
        add_identity_resolver(nodes, SUPERLOCAL_RESOLVER_BASE)
    return serialize_network(nodes, initial_superlocals)


def pack_bits(bits):
    padded = bits + "0" * (-len(bits) % 8)
    return bytes(
        int(padded[index:index + 8], 2)
        for index in range(0, len(padded), 8)
    )


def write_case(name, program, network):
    (OUTPUT_DIR / f"{name}.ubc").write_bytes(pack_bits(program))
    (OUTPUT_DIR / f"{name}.rn").write_bytes(network)
    print(f"{name}: {len(program)} program bits, {len(network)} RN bytes")


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
    add_identity_resolver(nodes, LOCAL_RESOLVER_BASE)
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


def generate_jump_to_position_1010():
    skipped = resize(20, 13)
    exit_offset = 0
    for _ in range(8):
        prefix = jump(exit_offset)
        new_offset = len(prefix) + len(skipped)
        if new_offset == exit_offset:
            break
        exit_offset = new_offset
    else:
        raise RuntimeError("jump target offset did not converge")

    program = prefix + skipped + "1011"
    network = build_network({"1010": 0x0A, "1011": 0x0B, "1100": 0x0C})
    write_case("jump_to_position_1010", program, network)


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


def generate_indirect_addressing_01():
    source_reference = direct_address(20, 0)
    destination_reference = direct_address(22, 13)
    first_value = int("1011010010110", 2)
    second_value = int("1100101101001", 2)
    program = (
        resize(20, 13)
        + resize(21, len(source_reference))
        + resize(22, 26)
        + resize(23, len(destination_reference))
        + copy(immediate(first_value), direct_address(20, 0))
        + copy(
            immediate(int(source_reference, 2)),
            direct_address(21, 0),
        )
        + copy(
            indirect_source(21, 0, 13),
            direct_address(22, 0),
        )
        + copy(
            immediate(int(destination_reference, 2)),
            direct_address(23, 0),
        )
        + copy(immediate(second_value), indirect_address(23, 0))
        + "1011"
    )
    network = build_network({"0101": 0x05, "1011": 0x0B, "1100": 0x0C})
    write_case("indirect_addressing_01", program, network)


def generate_foreign_addressing_11():
    depth_one = integer(1)
    depth_two = integer(2)
    depth_values = depth_one + depth_two
    middle_offset = 0
    inner_offset = 0

    for _ in range(16):
        root = (
            resize(20, 3)
            + resize(21, len(depth_values))
            + resize(10, 3, REG_LOCAL)
            + copy(
                immediate_sized(int(depth_values, 2), len(depth_values)),
                direct_address(21, 0),
            )
            + "0110" + direct_address(1, middle_offset)
            + copy(
                direct_source(10, 0, 3, REG_LOCAL),
                direct_address(20, 0),
            )
            + "1011"
        )
        middle = (
            resize(11, 3, REG_LOCAL)
            + copy(immediate_sized(5, 3),
                   direct_address(11, 0, REG_LOCAL))
            + "0110" + direct_address(1, inner_offset)
            + "1011"
        )
        inner = (
            copy(
                foreign_source(
                    direct_address(21, 0),
                    direct_address(11, 0, REG_LOCAL),
                    3,
                ),
                foreign_address(
                    direct_address(21, len(depth_one)),
                    direct_address(10, 0, REG_LOCAL),
                ),
            )
            + "1011"
        )
        new_middle_offset = len(root)
        new_inner_offset = len(root) + len(middle)
        if (new_middle_offset == middle_offset
                and new_inner_offset == inner_offset):
            break
        middle_offset = new_middle_offset
        inner_offset = new_inner_offset
    else:
        raise RuntimeError("foreign-addressing offsets did not converge")

    program = root + middle + inner
    network = build_network({
        "0101": 0x05,
        "0110": 0x06,
        "1011": 0x0B,
        "1100": 0x0C,
    })
    write_case("foreign_addressing_11", program, network)


def generate_procedure_register_class_00():
    value_bits = "1011010010110"
    value_offset = 0
    for _ in range(8):
        prefix = (
            resize(20, len(value_bits))
            + copy(
                direct_source(
                    31, value_offset, len(value_bits), REG_PROCEDURE
                ),
                direct_address(20, 0),
            )
            + "1011"
        )
        new_value_offset = len(prefix)
        if new_value_offset == value_offset:
            break
        value_offset = new_value_offset
    else:
        raise RuntimeError("procedure-register offset did not converge")

    network = build_network({"0101": 0x05, "1011": 0x0B, "1100": 0x0C})
    write_case("procedure_register_class_00", prefix + value_bits, network)


def generate_accumulated_prefixes_compute_0000_0001():
    inner = (
        read_prefix(1)
        + write_prefix(2)
        + compute(
            0,
            direct_source(11, 0, 3, REG_LOCAL),
            direct_source(12, 0, 3, REG_LOCAL),
            direct_address(10, 0, REG_LOCAL),
        )
        + "1011"
    )
    middle_offset = 0
    inner_offset = 0
    for _ in range(16):
        root = (
            resize(20, 3)
            + resize(10, 3, REG_LOCAL)
            + "0110" + direct_address(1, middle_offset)
            + copy(
                direct_source(10, 0, 3, REG_LOCAL),
                direct_address(20, 0),
            )
            + "1011"
        )
        middle = (
            resize(11, 3, REG_LOCAL)
            + resize(12, 3, REG_LOCAL)
            + copy(immediate(5), direct_address(11, 0, REG_LOCAL))
            + copy(immediate_sized(2, 3), direct_address(12, 0, REG_LOCAL))
            + "0110" + direct_address(1, inner_offset)
            + "1011"
        )
        new_middle_offset = len(root)
        new_inner_offset = len(root) + len(middle)
        if (new_middle_offset == middle_offset
                and new_inner_offset == inner_offset):
            break
        middle_offset = new_middle_offset
        inner_offset = new_inner_offset
    else:
        raise RuntimeError("prefix test offsets did not converge")

    program = root + middle + inner
    network = build_network({
        "0000": 0x00,
        "0001": 0x01,
        "0100": 0x04,
        "0101": 0x05,
        "0110": 0x06,
        "1011": 0x0B,
        "1100": 0x0C,
    })
    write_case("accumulated_prefixes_compute_0000_0001", program, network)


def generate_superlocal_registers():
    program = (
        resize(20, 64)
        + resize(5, 3, REG_SUPERLOCAL)
        + get_size(5, direct_address(20, 0), REG_SUPERLOCAL)
        + "1011"
    )
    network = build_network({
        "1011": 0x0B,
        "1100": (0x0C, SUPERLOCAL_RESOLVER_BASE),
        "1101": (0x0D, SUPERLOCAL_RESOLVER_BASE),
    }, superlocal=True)
    write_case("superlocal_registers", program, network)


def generate_write_prefix_0001():
    subroutine_offset = 0
    exit_offset = 0
    for _ in range(16):
        main = (
            "0110" + direct_address(1, subroutine_offset)
            + resize(20, 13)
        )
        new_exit_offset = len(main)
        subroutine = write_prefix(1) + jump(new_exit_offset) + "1011"
        new_subroutine_offset = new_exit_offset + 4
        if (new_exit_offset == exit_offset
                and new_subroutine_offset == subroutine_offset):
            break
        exit_offset = new_exit_offset
        subroutine_offset = new_subroutine_offset
    else:
        raise RuntimeError("write-prefix offsets did not converge")

    program = main + "1011" + subroutine
    network = build_network({
        "0001": 0x01,
        "0110": 0x06,
        "1010": 0x0A,
        "1011": 0x0B,
        "1100": 0x0C,
    })
    write_case("write_activation_record_0001", program, network)


def generate_conditional_prefix_0010():
    program = (
        resize(20, 1)
        + copy(immediate(0), direct_address(20, 0))
        + conditional_prefix(direct_address(20, 0))
        + resize(21, 7)
        + conditional_prefix(immediate(1))
        + resize(22, 9)
        + "1011"
    )
    network = build_network({
        "0010": 0x02,
        "0101": 0x05,
        "1011": 0x0B,
        "1100": 0x0C,
    })
    write_case("conditional_execution_0010", program, network)


def generate_procedure_node_type_1():
    subprocedure = return_immediate(37) + "1011"
    call_bits = "1110"
    call_node = command_leaf_index(call_bits)
    descriptor = (
        format(20, "05b")
        + format(2, "05b")
        + format(0, "016b")
    )
    program = (
        resize(20, len(subprocedure))
        + copy(
            immediate_sized(int(subprocedure, 2), len(subprocedure)),
            direct_address(20, 0),
        )
        + call_bits
        + "1011"
    )
    network = build_network(
        {
            "0101": 0x05,
            "1001": 0x09,
            "1011": 0x0B,
            "1100": 0x0C,
            call_bits: (0, SUPERLOCAL_RESOLVER_BASE, 1),
        },
        superlocal=True,
        initial_superlocals=((call_node, 0, descriptor),),
    )
    write_case("procedure_node_type_1", program, network)


def main():
    generate_call_new_procedure_0110()
    generate_call_new_network_0111()
    generate_call_new_procedure_and_network_1000()
    generate_resize_register_1100()
    generate_get_register_size_1101()
    generate_jump_to_position_1010()
    generate_return_result_1001()
    generate_copy_value_0101()
    generate_indirect_addressing_01()
    generate_foreign_addressing_11()
    generate_procedure_register_class_00()
    generate_accumulated_prefixes_compute_0000_0001()
    generate_superlocal_registers()
    generate_write_prefix_0001()
    generate_conditional_prefix_0010()
    generate_procedure_node_type_1()


if __name__ == "__main__":
    main()
