-- SPDX-License-Identifier: MIT
-- BradISA V1 VHDL Package
-- Opcodes, constants, and helper functions

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package bradisa_pkg is

    -- Opcodes
    constant OP_ADD  : std_logic_vector(3 downto 0) := x"0";
    constant OP_SUB  : std_logic_vector(3 downto 0) := x"1";
    constant OP_MUL  : std_logic_vector(3 downto 0) := x"2";
    constant OP_AND  : std_logic_vector(3 downto 0) := x"3";
    constant OP_OR   : std_logic_vector(3 downto 0) := x"4";
    constant OP_XOR  : std_logic_vector(3 downto 0) := x"5";
    constant OP_SHL  : std_logic_vector(3 downto 0) := x"6";
    constant OP_SHR  : std_logic_vector(3 downto 0) := x"7";
    constant OP_ADDI : std_logic_vector(3 downto 0) := x"8";
    constant OP_LDW  : std_logic_vector(3 downto 0) := x"9";
    constant OP_STW  : std_logic_vector(3 downto 0) := x"A";
    constant OP_BZ   : std_logic_vector(3 downto 0) := x"B";
    constant OP_BNZ  : std_logic_vector(3 downto 0) := x"C";
    constant OP_JMP  : std_logic_vector(3 downto 0) := x"D";
    constant OP_CALL : std_logic_vector(3 downto 0) := x"E";
    constant OP_RET  : std_logic_vector(3 downto 0) := x"F";

    -- Register numbers
    constant REG_R0 : std_logic_vector(3 downto 0) := x"0";
    constant REG_SP : std_logic_vector(3 downto 0) := x"D";
    constant REG_LR : std_logic_vector(3 downto 0) := x"E";
    constant REG_PC : std_logic_vector(3 downto 0) := x"F";

    -- Instruction field positions
    constant OPCODE_SHIFT : integer := 28;
    constant RD_SHIFT     : integer := 24;
    constant RS1_SHIFT    : integer := 20;
    constant RS2_SHIFT    : integer := 16;

    -- Pipeline model constants
    constant PHOENIX_DEPTH      : integer := 10;
    constant PHOENIX_EXEC_STAGE : integer := 6;
    constant FALCON_DEPTH       : integer := 5;
    constant FALCON_EXEC_STAGE  : integer := 3;

    -- RET fixed encoding
    constant RET_RAW : std_logic_vector(31 downto 0) := x"F0000000";

    -- Helper: sign-extend 16-bit to 32-bit
    function sext16(v : std_logic_vector(15 downto 0)) return std_logic_vector;

end package bradisa_pkg;

package body bradisa_pkg is
    function sext16(v : std_logic_vector(15 downto 0)) return std_logic_vector is
    begin
        return (31 downto 16 => v(15)) & v;
    end function;
end package body bradisa_pkg;
