-- SPDX-License-Identifier: MIT
-- BradISA V1 -- Falcon-Lite 3-stage ultra-tiny core (VHDL)

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.bradisa_pkg.all;

entity falcon_lite_core is
    port (
        clk         : in  std_logic;
        rst_n       : in  std_logic;
        imem_addr   : out std_logic_vector(31 downto 0);
        imem_rdata  : in  std_logic_vector(31 downto 0);
        dmem_addr   : out std_logic_vector(31 downto 0);
        dmem_req    : out std_logic;
        dmem_we     : out std_logic;
        dmem_wdata  : out std_logic_vector(31 downto 0);
        dmem_rdata  : in  std_logic_vector(31 downto 0)
    );
end entity falcon_lite_core;

architecture rtl of falcon_lite_core is

    -- 3-stage pipeline: F -> D -> EX
    -- Single-issue, in-order
    -- Embedded regfile: 8 registers (r0..r7), r0 hardwired to zero

    -- ─── Register file (embedded, 8 x 32-bit) ──────────────
    type reg_array is array (0 to 7) of std_logic_vector(31 downto 0);
    signal regs : reg_array := (others => (others => '0'));

    -- ─── Fetch stage ────────────────────────────────────────
    signal f_pc   : std_logic_vector(31 downto 0);
    signal f_insn : std_logic_vector(31 downto 0);
    signal f_valid : std_logic;

    -- ─── Decode stage ───────────────────────────────────────
    signal d_valid   : std_logic;
    signal d_pc      : std_logic_vector(31 downto 0);
    signal d_opcode  : std_logic_vector(3 downto 0);
    signal d_rd      : std_logic_vector(3 downto 0);
    signal d_rs1     : std_logic_vector(3 downto 0);
    signal d_rs2     : std_logic_vector(3 downto 0);
    signal d_rs1_val : std_logic_vector(31 downto 0);
    signal d_rs2_val : std_logic_vector(31 downto 0);
    signal d_imm     : std_logic_vector(31 downto 0);
    signal d_reg_we  : std_logic;

    -- ─── Execute stage ──────────────────────────────────────
    signal e_valid      : std_logic;
    signal e_rd         : std_logic_vector(3 downto 0);
    signal e_result     : std_logic_vector(31 downto 0);
    signal e_addr       : std_logic_vector(31 downto 0);
    signal e_reg_we     : std_logic;
    signal e_mem_req    : std_logic;
    signal e_mem_we     : std_logic;
    signal e_store_data : std_logic_vector(31 downto 0);

    -- ─── PC / branch ────────────────────────────────────────
    signal pc           : std_logic_vector(31 downto 0);
    signal next_pc      : std_logic_vector(31 downto 0);
    signal branch_taken : std_logic;

    -- ─── ALU result ─────────────────────────────────────────
    signal alu_result : std_logic_vector(31 downto 0);

    -- ─── Operand reads from embedded regfile ────────────────
    signal rs1_idx : integer range 0 to 7;
    signal rs2_idx : integer range 0 to 7;
    signal rd_idx  : integer range 0 to 7;

    signal raw_rd  : std_logic_vector(3 downto 0);
    signal raw_rs1 : std_logic_vector(3 downto 0);
    signal raw_rs2 : std_logic_vector(3 downto 0);

begin

    -- ─── Instruction field extraction ───────────────────────
    raw_rd  <= f_insn(27 downto 24);
    raw_rs1 <= f_insn(23 downto 20);
    raw_rs2 <= f_insn(19 downto 16);

    -- ─── Fetch stage ────────────────────────────────────────
    imem_addr <= pc;

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            pc      <= (others => '0');
            f_valid <= '0';
            f_pc    <= (others => '0');
            f_insn  <= (others => '0');
        elsif rising_edge(clk) then
            if branch_taken = '1' then
                f_valid <= '0';
                pc      <= next_pc;
            else
                f_valid <= '1';
                f_pc    <= pc;
                f_insn  <= imem_rdata;
                pc      <= std_logic_vector(unsigned(pc) + 4);
            end if;
        end if;
    end process;

    -- ─── Decode stage ───────────────────────────────────────
    rs1_idx <= to_integer(unsigned(d_rs1(2 downto 0)));
    rs2_idx <= to_integer(unsigned(d_rs2(2 downto 0)));

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            d_valid   <= '0';
            d_pc      <= (others => '0');
            d_opcode  <= (others => '0');
            d_rd      <= (others => '0');
            d_rs1     <= (others => '0');
            d_rs2     <= (others => '0');
            d_rs1_val <= (others => '0');
            d_rs2_val <= (others => '0');
            d_imm     <= (others => '0');
            d_reg_we  <= '0';
        elsif rising_edge(clk) then
            if branch_taken = '1' then
                d_valid <= '0';
            else
                d_valid   <= f_valid;
                d_pc      <= f_pc;
                d_opcode  <= f_insn(31 downto 28);
                d_rd      <= raw_rd;
                d_rs1     <= raw_rs1;
                d_rs2     <= raw_rs2;
                d_rs1_val <= (others => '0') when raw_rs1(3) = '1' or unsigned(raw_rs1(2 downto 0)) = 0 else
                             regs(to_integer(unsigned(raw_rs1(2 downto 0))));
                d_rs2_val <= (others => '0') when raw_rs2(3) = '1' or unsigned(raw_rs2(2 downto 0)) = 0 else
                             regs(to_integer(unsigned(raw_rs2(2 downto 0))));
                d_imm     <= sext16(f_insn(15 downto 0));
                d_reg_we  <= '1' when (unsigned(f_insn(31 downto 28)) <= 8) or
                                       (f_insn(31 downto 28) = OP_LDW) else '0';
            end if;
        end if;
    end process;

    -- ─── ALU ─────────────────────────────────────────────────
    alu_inst : entity work.brad_alu
        port map (a => d_rs1_val, b => d_rs2_val, op => d_opcode, result => alu_result);

    -- ─── Branch resolution ───────────────────────────────────
    branch_taken <= d_valid and (
        ('1' when d_opcode = OP_BZ  and d_rs1_val = x"00000000" else '0') or
        ('1' when d_opcode = OP_BNZ and d_rs1_val /= x"00000000" else '0') or
        ('1' when d_opcode = OP_JMP  else '0') or
        ('1' when d_opcode = OP_CALL else '0') or
        ('1' when d_opcode = OP_RET  else '0'));
    next_pc <= d_rs1_val when d_opcode = OP_RET else
               std_logic_vector(unsigned(d_pc) + 4 + unsigned(d_imm));

    -- ─── Execute stage ──────────────────────────────────────
    process(clk, rst_n)
        variable ex_result : std_logic_vector(31 downto 0);
    begin
        if rst_n = '0' then
            e_valid      <= '0';
            e_rd         <= (others => '0');
            e_result     <= (others => '0');
            e_addr       <= (others => '0');
            e_reg_we     <= '0';
            e_mem_req    <= '0';
            e_mem_we     <= '0';
            e_store_data <= (others => '0');
        elsif rising_edge(clk) then
            e_valid <= d_valid and not branch_taken;
            e_rd    <= d_rd;
            e_reg_we <= d_reg_we;

            if unsigned(d_opcode) <= 7 then
                ex_result := alu_result;
            elsif d_opcode = OP_ADDI then
                ex_result := std_logic_vector(unsigned(d_rs1_val) + unsigned(d_imm));
            else
                ex_result := (others => '0');
            end if;

            if d_opcode = OP_CALL then
                ex_result := std_logic_vector(unsigned(d_pc) + 4);
                e_rd      <= REG_LR;
                e_reg_we  <= '1';
            end if;

            if d_opcode = OP_MUL then
                ex_result := (others => '0');
            end if;

            e_result     <= ex_result;
            e_addr       <= std_logic_vector(unsigned(d_rs1_val) + unsigned(d_imm));
            e_store_data <= d_rs2_val;
            e_mem_req    <= '1' when (d_opcode = OP_LDW or d_opcode = OP_STW) else '0';
            e_mem_we     <= '1' when d_opcode = OP_STW else '0';
        end if;
    end process;

    -- ─── Writeback to embedded regfile ───────────────────────
    rd_idx <= to_integer(unsigned(e_rd(2 downto 0)));

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            regs <= (others => (others => '0'));
        elsif rising_edge(clk) then
            if e_valid = '1' and e_reg_we = '1' and
               unsigned(e_rd(2 downto 0)) /= 0 and e_rd(3) = '0' then
                if e_mem_req = '1' and e_mem_we = '0' then
                    regs(rd_idx) <= dmem_rdata;
                else
                    regs(rd_idx) <= e_result;
                end if;
            end if;
        end if;
    end process;

    -- ─── Memory port outputs ─────────────────────────────────
    dmem_addr  <= e_addr;
    dmem_req   <= e_mem_req;
    dmem_we    <= e_mem_we;
    dmem_wdata <= e_store_data;

end architecture rtl;
