-- SPDX-License-Identifier: MIT
-- BradISA V1 -- BradCore (Falcon 5-stage in-order pipeline, VHDL)
-- Synthesisable for FPGA.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.bradisa_pkg.all;

entity brad_core is
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
end entity brad_core;

architecture rtl of brad_core is

    -- Pipeline registers: Fetch
    signal f_valid : std_logic;
    signal f_pc    : std_logic_vector(31 downto 0);
    signal f_insn  : std_logic_vector(31 downto 0);

    -- Pipeline registers: Decode
    signal d_valid    : std_logic;
    signal d_pc       : std_logic_vector(31 downto 0);
    signal d_opcode   : std_logic_vector(3 downto 0);
    signal d_rd       : std_logic_vector(3 downto 0);
    signal d_rs1      : std_logic_vector(3 downto 0);
    signal d_rs2      : std_logic_vector(3 downto 0);
    signal d_rs1_val  : std_logic_vector(31 downto 0);
    signal d_rs2_val  : std_logic_vector(31 downto 0);
    signal d_imm      : std_logic_vector(31 downto 0);
    signal d_reg_we   : std_logic;

    -- Pipeline registers: Execute+Writeback
    signal e_valid      : std_logic;
    signal e_rd         : std_logic_vector(3 downto 0);
    signal e_result     : std_logic_vector(31 downto 0);
    signal e_addr       : std_logic_vector(31 downto 0);
    signal e_reg_we     : std_logic;
    signal e_mem_req    : std_logic;
    signal e_mem_we     : std_logic;
    signal e_store_data : std_logic_vector(31 downto 0);

    -- PC
    signal pc          : std_logic_vector(31 downto 0);
    signal next_pc     : std_logic_vector(31 downto 0);
    signal branch_taken : std_logic;

    -- Register file
    signal rf_rdata1 : std_logic_vector(31 downto 0);
    signal rf_rdata2 : std_logic_vector(31 downto 0);
    signal rf_we     : std_logic;
    signal rf_waddr  : std_logic_vector(3 downto 0);
    signal rf_wdata  : std_logic_vector(31 downto 0);

    -- RAW hazard
    signal raw_hazard : std_logic;
    signal stall      : std_logic;

    -- Branch resolution
    signal bz_taken   : std_logic;
    signal bnz_taken  : std_logic;
    signal jmp_taken  : std_logic;
    signal call_taken : std_logic;
    signal ret_taken  : std_logic;
    signal br_target  : std_logic_vector(31 downto 0);

    -- Decode field extraction
    signal raw_op  : std_logic_vector(3 downto 0);
    signal raw_rd  : std_logic_vector(3 downto 0);
    signal raw_rs1 : std_logic_vector(3 downto 0);
    signal raw_rs2 : std_logic_vector(3 downto 0);
    signal raw_imm : std_logic_vector(15 downto 0);

    -- ALU result
    signal alu_result : std_logic_vector(31 downto 0);

    -- ADDI result
    signal addi_result : std_logic_vector(31 downto 0);

begin

    -- ─── Instruction field extraction ────────────────────────
    raw_op  <= f_insn(31 downto 28);
    raw_rd  <= f_insn(27 downto 24);
    raw_rs1 <= f_insn(23 downto 20);
    raw_rs2 <= f_insn(19 downto 16);
    raw_imm <= f_insn(15 downto 0);

    -- ─── Register file instance ──────────────────────────────
    regfile_inst : entity work.brad_regfile
        port map (
            clk    => clk,
            rst_n  => rst_n,
            raddr1 => d_rs1,
            rdata1 => rf_rdata1,
            raddr2 => d_rs2,
            rdata2 => rf_rdata2,
            we     => rf_we,
            waddr  => rf_waddr,
            wdata  => rf_wdata
        );

    -- ─── Hazard detection ────────────────────────────────────
    raw_hazard <= '1' when (d_valid = '1' and e_valid = '1' and e_reg_we = '1'
                            and e_rd /= REG_R0
                            and (d_rs1 = e_rd or (d_opcode = OP_STW and d_rs2 = e_rd)))
                   else '0';
    stall <= raw_hazard;

    -- ─── Fetch stage ─────────────────────────────────────────
    imem_addr <= pc;

    process(clk, rst_n) begin
        if rst_n = '0' then
            pc      <= (others => '0');
            f_valid <= '0';
            f_pc    <= (others => '0');
            f_insn  <= (others => '0');
        elsif rising_edge(clk) then
            if branch_taken = '1' then
                f_valid <= '0';
                pc      <= next_pc;
            elsif stall = '0' then
                f_valid <= '1';
                f_pc    <= pc;
                f_insn  <= imem_rdata;
                pc      <= std_logic_vector(unsigned(pc) + 4);
            end if;
        end if;
    end process;

    -- ─── Decode stage ────────────────────────────────────────
    addi_result <= std_logic_vector(unsigned(d_rs1_val) + unsigned(d_imm));

    process(clk, rst_n) begin
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
            elsif stall = '0' then
                d_valid   <= f_valid;
                d_pc      <= f_pc;
                d_opcode  <= raw_op;
                d_rd      <= raw_rd;
                d_rs1     <= raw_rs1;
                d_rs2     <= raw_rs2;
                d_rs1_val <= rf_rdata1;
                d_rs2_val <= rf_rdata2;
                d_imm     <= sext16(raw_imm);
                d_reg_we  <= '1' when (unsigned(raw_op) <= 8) or (raw_op = OP_LDW) else '0';
            end if;
        end if;
    end process;

    -- ─── ALU ─────────────────────────────────────────────────
    alu_inst : entity work.brad_alu
        port map (a => d_rs1_val, b => d_rs2_val, op => d_opcode, result => alu_result);

    -- ─── Branch resolution ───────────────────────────────────
    bz_taken   <= '1' when d_opcode = OP_BZ  and d_rs1_val = x"00000000" else '0';
    bnz_taken  <= '1' when d_opcode = OP_BNZ and d_rs1_val /= x"00000000" else '0';
    jmp_taken  <= '1' when d_opcode = OP_JMP  else '0';
    call_taken <= '1' when d_opcode = OP_CALL else '0';
    ret_taken  <= '1' when d_opcode = OP_RET  else '0';

    branch_taken <= d_valid and (bz_taken or bnz_taken or jmp_taken or call_taken or ret_taken);
    br_target   <= std_logic_vector(unsigned(d_pc) + 4 + unsigned(d_imm));
    next_pc     <= d_rs1_val when ret_taken = '1' else br_target;

    -- ─── Execute + Writeback stage ──────────────────────────
    process(clk, rst_n) begin
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
            if stall = '0' then
                e_valid <= d_valid and not branch_taken;
                e_rd    <= d_rd;
                e_reg_we <= d_reg_we;

                if unsigned(d_opcode) <= 7 then
                    e_result <= alu_result;
                elsif d_opcode = OP_ADDI then
                    e_result <= addi_result;
                else
                    e_result <= (others => '0');
                end if;

                if d_opcode = OP_CALL then
                    e_result <= std_logic_vector(unsigned(d_pc) + 4);
                    e_rd     <= REG_LR;
                    e_reg_we <= '1';
                end if;

                e_addr       <= std_logic_vector(unsigned(d_rs1_val) + unsigned(d_imm));
                e_store_data <= d_rs2_val;
                e_mem_req    <= '1' when (d_opcode = OP_LDW or d_opcode = OP_STW) else '0';
                e_mem_we     <= '1' when d_opcode = OP_STW else '0';
            end if;
        end if;
    end process;

    -- ─── Writeback to register file ──────────────────────────
    rf_we    <= e_valid and e_reg_we;
    rf_waddr <= e_rd;
    rf_wdata <= dmem_rdata when (e_mem_req = '1' and e_mem_we = '0') else e_result;

    -- Memory port outputs
    dmem_addr  <= e_addr;
    dmem_req   <= e_mem_req;
    dmem_we    <= e_mem_we;
    dmem_wdata <= e_store_data;

end architecture rtl;
