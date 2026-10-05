-- SPDX-License-Identifier: MIT
-- BradISA V1 -- Kestrel 8-stage dual-issue in-order core (VHDL)
-- Pipeline: F1->F2->D1->D2->EX1->EX2->M->WB
-- Dual-issue: ALU+ALU or ALU+LDW/STW pairing
-- 16-entry BTB with BTFNT static predictor

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.bradisa_pkg.all;

entity kestrel_core is
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
end entity kestrel_core;

architecture rtl of kestrel_core is

    -- ─── F1: PC stage ────────────────────────────────────────
    signal f1_pc    : std_logic_vector(31 downto 0);
    signal f1_valid : std_logic;

    -- ─── F2: instruction fetch stage ─────────────────────────
    signal f2_pc    : std_logic_vector(31 downto 0);
    signal f2_valid : std_logic;
    signal f2_insn  : std_logic_vector(31 downto 0);

    -- ─── D1: decode stage (slot 0) ───────────────────────────
    signal d1_valid   : std_logic;
    signal d1_pc      : std_logic_vector(31 downto 0);
    signal d1_opcode  : std_logic_vector(3 downto 0);
    signal d1_rd      : std_logic_vector(3 downto 0);
    signal d1_rs1     : std_logic_vector(3 downto 0);
    signal d1_rs2     : std_logic_vector(3 downto 0);
    signal d1_rs1_val : std_logic_vector(31 downto 0);
    signal d1_rs2_val : std_logic_vector(31 downto 0);
    signal d1_imm     : std_logic_vector(31 downto 0);
    signal d1_reg_we  : std_logic;

    -- ─── D2: decode stage (slot 1 + pairing) ─────────────────
    signal d2_s0_valid   : std_logic;
    signal d2_s0_pc      : std_logic_vector(31 downto 0);
    signal d2_s0_opcode  : std_logic_vector(3 downto 0);
    signal d2_s0_rd      : std_logic_vector(3 downto 0);
    signal d2_s0_rs1_val : std_logic_vector(31 downto 0);
    signal d2_s0_rs2_val : std_logic_vector(31 downto 0);
    signal d2_s0_imm     : std_logic_vector(31 downto 0);
    signal d2_s0_reg_we  : std_logic;

    signal d2_s1_valid   : std_logic;
    signal d2_s1_pc      : std_logic_vector(31 downto 0);
    signal d2_s1_opcode  : std_logic_vector(3 downto 0);
    signal d2_s1_rd      : std_logic_vector(3 downto 0);
    signal d2_s1_rs1     : std_logic_vector(3 downto 0);
    signal d2_s1_rs2     : std_logic_vector(3 downto 0);
    signal d2_s1_rs1_val : std_logic_vector(31 downto 0);
    signal d2_s1_rs2_val : std_logic_vector(31 downto 0);
    signal d2_s1_imm     : std_logic_vector(31 downto 0);
    signal d2_s1_reg_we  : std_logic;

    signal d2_paired : std_logic;

    -- ─── EX1: execute slot 0 ─────────────────────────────────
    signal ex1_valid      : std_logic;
    signal ex1_rd         : std_logic_vector(3 downto 0);
    signal ex1_result     : std_logic_vector(31 downto 0);
    signal ex1_addr       : std_logic_vector(31 downto 0);
    signal ex1_reg_we     : std_logic;
    signal ex1_mem_req    : std_logic;
    signal ex1_mem_we     : std_logic;
    signal ex1_store_data : std_logic_vector(31 downto 0);

    -- ─── EX2: execute slot 1 + memory ────────────────────────
    signal ex2_s0_rd        : std_logic_vector(3 downto 0);
    signal ex2_s0_result    : std_logic_vector(31 downto 0);
    signal ex2_s0_reg_we    : std_logic;
    signal ex2_s1_valid     : std_logic;
    signal ex2_s1_rd        : std_logic_vector(3 downto 0);
    signal ex2_s1_result    : std_logic_vector(31 downto 0);
    signal ex2_s1_reg_we    : std_logic;
    signal ex2_s1_addr      : std_logic_vector(31 downto 0);
    signal ex2_s1_mem_req   : std_logic;
    signal ex2_s1_mem_we    : std_logic;
    signal ex2_s1_store_data : std_logic_vector(31 downto 0);

    -- ─── M: memory stage ─────────────────────────────────────
    signal m_s0_rd     : std_logic_vector(3 downto 0);
    signal m_s0_result : std_logic_vector(31 downto 0);
    signal m_s0_reg_we : std_logic;
    signal m_s1_rd     : std_logic_vector(3 downto 0);
    signal m_s1_result : std_logic_vector(31 downto 0);
    signal m_s1_reg_we : std_logic;

    -- ─── WB: writeback stage ─────────────────────────────────
    signal wb_s0_rd     : std_logic_vector(3 downto 0);
    signal wb_s0_result : std_logic_vector(31 downto 0);
    signal wb_s0_reg_we : std_logic;
    signal wb_s1_rd     : std_logic_vector(3 downto 0);
    signal wb_s1_result : std_logic_vector(31 downto 0);
    signal wb_s1_reg_we : std_logic;

    -- ─── Dual regfile instances (shadowed, 4 read ports) ────
    signal rf0_rdata1 : std_logic_vector(31 downto 0);
    signal rf0_rdata2 : std_logic_vector(31 downto 0);
    signal rf1_rdata1 : std_logic_vector(31 downto 0);
    signal rf1_rdata2 : std_logic_vector(31 downto 0);
    signal wb0_we     : std_logic;
    signal wb0_waddr  : std_logic_vector(3 downto 0);
    signal wb0_wdata  : std_logic_vector(31 downto 0);
    signal wb1_we     : std_logic;
    signal wb1_waddr  : std_logic_vector(3 downto 0);
    signal wb1_wdata  : std_logic_vector(31 downto 0);

    -- ─── Control signals ─────────────────────────────────────
    signal stall_d1    : std_logic;
    signal flush       : std_logic;
    signal branch_taken : std_logic;
    signal br_target   : std_logic_vector(31 downto 0);
    signal next_pc     : std_logic_vector(31 downto 0);

    -- ─── ALU results ─────────────────────────────────────────
    signal alu0_result : std_logic_vector(31 downto 0);
    signal alu1_result : std_logic_vector(31 downto 0);

    -- ─── BTB: 16 entries ─────────────────────────────────────
    type btb_entry_t is record
        valid  : std_logic;
        tag    : std_logic_vector(25 downto 0);
        target : std_logic_vector(31 downto 0);
    end record;
    type btb_array_t is array (0 to 15) of btb_entry_t;
    signal btb : btb_array_t := (others => (valid => '0', tag => (others => '0'), target => (others => '0')));

    signal btb_index   : integer range 0 to 15;
    signal btb_hit     : std_logic;
    signal btb_target  : std_logic_vector(31 downto 0);
    signal pred_taken  : std_logic;

    -- ─── Pairing helpers ────────────────────────────────────
    signal pair_ok      : std_logic;
    signal slot0_is_branch : std_logic;
    signal slot0_is_mem : std_logic;
    signal slot1_is_mem : std_logic;
    signal raw_s0_to_s1 : std_logic;

    -- ─── Hazard helpers ──────────────────────────────────────
    signal load_to_use    : std_logic;
    signal forward_ex1_en : std_logic;
    signal forward_ex1_rd : std_logic_vector(3 downto 0);
    signal forward_ex1_val : std_logic_vector(31 downto 0);

    -- ─── Slot 1 raw decode (from F2) ────────────────────────
    signal raw_s1_op  : std_logic_vector(3 downto 0);
    signal raw_s1_rd  : std_logic_vector(3 downto 0);
    signal raw_s1_rs1 : std_logic_vector(3 downto 0);
    signal raw_s1_rs2 : std_logic_vector(3 downto 0);
    signal raw_s1_imm : std_logic_vector(15 downto 0);

    -- ─── Combinational ALU0/ADDI helpers ────────────────────
    signal s0_alu_out  : std_logic_vector(31 downto 0);
    signal s0_addi_out : std_logic_vector(31 downto 0);

    -- ─── Combinational ALU1/ADDI helpers ────────────────────
    signal s1_alu_out  : std_logic_vector(31 downto 0);
    signal s1_addi_out : std_logic_vector(31 downto 0);

begin

    -- ─── Slot 1 raw decode (combinational from F2) ──────────
    raw_s1_op  <= f2_insn(31 downto 28);
    raw_s1_rd  <= f2_insn(27 downto 24);
    raw_s1_rs1 <= f2_insn(23 downto 20);
    raw_s1_rs2 <= f2_insn(19 downto 16);
    raw_s1_imm <= f2_insn(15 downto 0);

    -- ─── Dual regfile instances ─────────────────────────────
    -- Instance 0: serves D1 (slot 0) reads
    regfile0_inst : entity work.brad_regfile
        port map (
            clk    => clk,
            rst_n  => rst_n,
            raddr1 => d1_rs1,
            rdata1 => rf0_rdata1,
            raddr2 => d1_rs2,
            rdata2 => rf0_rdata2,
            we     => wb0_we,
            waddr  => wb0_waddr,
            wdata  => wb0_wdata
        );

    -- Instance 1: serves D2 slot 1 reads
    regfile1_inst : entity work.brad_regfile
        port map (
            clk    => clk,
            rst_n  => rst_n,
            raddr1 => raw_s1_rs1,
            rdata1 => rf1_rdata1,
            raddr2 => raw_s1_rs2,
            rdata2 => rf1_rdata2,
            we     => wb1_we,
            waddr  => wb1_waddr,
            wdata  => wb1_wdata
        );

    -- ─── Writeback coherence ────────────────────────────────
    -- Both instances shadow the same architectural state.
    -- Write to instance 0 with slot 0's write, instance 1 with slot 1's write.
    -- If only one writes, write both with that value for coherence.
    process(wb_s0_reg_we, wb_s0_rd, wb_s0_result,
            wb_s1_reg_we, wb_s1_rd, wb_s1_result)
        variable s0_writes : std_logic;
        variable s1_writes : std_logic;
    begin
        s0_writes := wb_s0_reg_we and (unsigned(wb_s0_rd) /= 0);
        s1_writes := wb_s1_reg_we and (unsigned(wb_s1_rd) /= 0);

        if s1_writes = '1' then
            wb1_we    <= '1';
            wb1_waddr <= wb_s1_rd;
            wb1_wdata <= wb_s1_result;
        else
            wb1_we    <= '0';
            wb1_waddr <= (others => '0');
            wb1_wdata <= (others => '0');
        end if;

        if s0_writes = '1' and s1_writes = '0' then
            wb0_we    <= '1';
            wb0_waddr <= wb_s0_rd;
            wb0_wdata <= wb_s0_result;
        elsif s1_writes = '1' then
            wb0_we    <= '1';
            wb0_waddr <= wb_s1_rd;
            wb0_wdata <= wb_s1_result;
        else
            wb0_we    <= '0';
            wb0_waddr <= (others => '0');
            wb0_wdata <= (others => '0');
        end if;
    end process;

    -- ─── BTB read (F1 stage combinational) ──────────────────
    btb_index <= to_integer(unsigned(f1_pc(5 downto 2)));
    btb_hit   <= btb(btb_index).valid and
                 (btb(btb_index).tag = f1_pc(31 downto 6));
    btb_target <= btb(btb_index).target;

    -- BTFNT: backward conditional branch (signed imm < 0) => predict taken
    pred_taken <= btb_hit and
                  (signed(f2_insn(15 downto 0)) < 0) and
                  (f2_insn(31 downto 28) = OP_BZ or f2_insn(31 downto 28) = OP_BNZ);

    -- ─── F1 stage ────────────────────────────────────────────
    imem_addr <= f1_pc;

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            f1_valid <= '0';
            f1_pc    <= (others => '0');
        elsif rising_edge(clk) then
            if flush = '1' then
                f1_valid <= '0';
                f1_pc    <= next_pc;
            elsif stall_d1 = '0' then
                if pred_taken = '1' then
                    f1_pc <= btb_target;
                else
                    f1_pc <= std_logic_vector(unsigned(f1_pc) + 4);
                end if;
                f1_valid <= '1';
            end if;
        end if;
    end process;

    -- ─── F2 stage ────────────────────────────────────────────
    process(clk, rst_n)
    begin
        if rst_n = '0' then
            f2_valid <= '0';
            f2_pc    <= (others => '0');
            f2_insn  <= (others => '0');
        elsif rising_edge(clk) then
            if flush = '1' then
                f2_valid <= '0';
            elsif stall_d1 = '0' then
                f2_valid <= f1_valid;
                f2_pc    <= f1_pc;
                f2_insn  <= imem_rdata;
            end if;
        end if;
    end process;

    -- ─── Forwarding: EX1 result → D1 slot 0 operands ───────
    forward_ex1_en <= ex1_valid and ex1_reg_we and (unsigned(ex1_rd) /= 0);
    forward_ex1_rd <= ex1_rd;
    forward_ex1_val <= ex1_result;

    -- ─── Load-to-use hazard (EX2 loads -> D1 reads) ─────────
    load_to_use <= '1' when (ex2_s0_reg_we = '1' and unsigned(ex2_s0_rd) /= 0 and
                             d1_valid = '1' and
                             (d1_rs1 = ex2_s0_rd or d1_rs2 = ex2_s0_rd))
                    else '0';
    stall_d1 <= load_to_use;

    -- ─── D1 stage (slot 0 decode) ────────────────────────────
    process(clk, rst_n)
        variable v_rs1_val : std_logic_vector(31 downto 0);
        variable v_rs2_val : std_logic_vector(31 downto 0);
    begin
        if rst_n = '0' then
            d1_valid   <= '0';
            d1_pc      <= (others => '0');
            d1_opcode  <= (others => '0');
            d1_rd      <= (others => '0');
            d1_rs1     <= (others => '0');
            d1_rs2     <= (others => '0');
            d1_rs1_val <= (others => '0');
            d1_rs2_val <= (others => '0');
            d1_imm     <= (others => '0');
            d1_reg_we  <= '0';
        elsif rising_edge(clk) then
            if flush = '1' then
                d1_valid <= '0';
            elsif stall_d1 = '0' then
                d1_valid   <= f2_valid;
                d1_pc      <= f2_pc;
                d1_opcode  <= f2_insn(31 downto 28);
                d1_rd      <= f2_insn(27 downto 24);
                d1_rs1     <= f2_insn(23 downto 20);
                d1_rs2     <= f2_insn(19 downto 16);

                v_rs1_val := rf0_rdata1;
                v_rs2_val := rf0_rdata2;
                if forward_ex1_en = '1' and
                   f2_insn(23 downto 20) = forward_ex1_rd then
                    v_rs1_val := forward_ex1_val;
                end if;
                if forward_ex1_en = '1' and
                   f2_insn(19 downto 16) = forward_ex1_rd then
                    v_rs2_val := forward_ex1_val;
                end if;
                d1_rs1_val <= v_rs1_val;
                d1_rs2_val <= v_rs2_val;

                d1_imm    <= sext16(f2_insn(15 downto 0));
                d1_reg_we <= '1' when (unsigned(f2_insn(31 downto 28)) <= 8) or
                                      (f2_insn(31 downto 28) = OP_LDW) else '0';
            end if;
        end if;
    end process;

    -- ─── Pairing checks (combinational) ─────────────────────
    slot0_is_branch <= '1' when d1_opcode = OP_BZ or d1_opcode = OP_BNZ or
                                 d1_opcode = OP_JMP or d1_opcode = OP_CALL or
                                 d1_opcode = OP_RET else '0';
    slot0_is_mem <= '1' when d1_opcode = OP_LDW or d1_opcode = OP_STW else '0';
    slot1_is_mem <= '1' when raw_s1_op = OP_LDW or raw_s1_op = OP_STW else '0';
    raw_s0_to_s1 <= '1' when unsigned(d1_rd) /= 0 and
                            (unsigned(d1_rd) = unsigned(raw_s1_rs1) or
                             unsigned(d1_rd) = unsigned(raw_s1_rs2)) else '0';

    pair_ok <= '1' when f2_valid = '1' and d1_valid = '1' and
                         slot0_is_branch = '0' and raw_s0_to_s1 = '0' and
                         not (slot0_is_mem = '1' and slot1_is_mem = '1') else '0';

    -- ─── D2 stage (slot 1 decode + pairing) ─────────────────
    process(clk, rst_n)
    begin
        if rst_n = '0' then
            d2_s0_valid   <= '0';
            d2_s0_pc      <= (others => '0');
            d2_s0_opcode  <= (others => '0');
            d2_s0_rd      <= (others => '0');
            d2_s0_rs1_val <= (others => '0');
            d2_s0_rs2_val <= (others => '0');
            d2_s0_imm     <= (others => '0');
            d2_s0_reg_we  <= '0';
            d2_s1_valid   <= '0';
            d2_s1_pc      <= (others => '0');
            d2_s1_opcode  <= (others => '0');
            d2_s1_rd      <= (others => '0');
            d2_s1_rs1     <= (others => '0');
            d2_s1_rs2     <= (others => '0');
            d2_s1_rs1_val <= (others => '0');
            d2_s1_rs2_val <= (others => '0');
            d2_s1_imm     <= (others => '0');
            d2_s1_reg_we  <= '0';
            d2_paired     <= '0';
        elsif rising_edge(clk) then
            if flush = '1' then
                d2_s0_valid <= '0';
                d2_s1_valid <= '0';
                d2_paired   <= '0';
            else
                -- Slot 0 always passes through from D1
                d2_s0_valid   <= d1_valid;
                d2_s0_pc      <= d1_pc;
                d2_s0_opcode  <= d1_opcode;
                d2_s0_rd      <= d1_rd;
                d2_s0_rs1_val <= d1_rs1_val;
                d2_s0_rs2_val <= d1_rs2_val;
                d2_s0_imm     <= d1_imm;
                d2_s0_reg_we  <= d1_reg_we;

                -- Slot 1 paired from F2
                d2_paired   <= pair_ok;
                d2_s1_valid <= pair_ok and f2_valid;
                d2_s1_pc    <= std_logic_vector(unsigned(d1_pc) + 4);
                d2_s1_opcode  <= raw_s1_op;
                d2_s1_rd      <= raw_s1_rd;
                d2_s1_rs1     <= raw_s1_rs1;
                d2_s1_rs2     <= raw_s1_rs2;
                d2_s1_rs1_val <= rf1_rdata1;
                d2_s1_rs2_val <= rf1_rdata2;
                d2_s1_imm     <= sext16(raw_s1_imm);
                d2_s1_reg_we  <= '1' when (unsigned(raw_s1_op) <= 8) or
                                          (raw_s1_op = OP_LDW) else '0';
            end if;
        end if;
    end process;

    -- ─── ALU0 (slot 0) ──────────────────────────────────────
    alu0_inst : entity work.brad_alu
        port map (a => d2_s0_rs1_val, b => d2_s0_rs2_val,
                  op => d2_s0_opcode, result => alu0_result);

    s0_addi_out <= std_logic_vector(unsigned(d2_s0_rs1_val) + unsigned(d2_s0_imm));

    -- ─── ALU1 (slot 1) ──────────────────────────────────────
    alu1_inst : entity work.brad_alu
        port map (a => d2_s1_rs1_val, b => d2_s1_rs2_val,
                  op => d2_s1_opcode, result => alu1_result);

    s1_addi_out <= std_logic_vector(unsigned(d2_s1_rs1_val) + unsigned(d2_s1_imm));

    -- ─── EX1 stage (slot 0 execution) ────────────────────────
    process(clk, rst_n)
        variable res : std_logic_vector(31 downto 0);
    begin
        if rst_n = '0' then
            ex1_valid      <= '0';
            ex1_rd         <= (others => '0');
            ex1_result     <= (others => '0');
            ex1_addr       <= (others => '0');
            ex1_reg_we     <= '0';
            ex1_mem_req    <= '0';
            ex1_mem_we     <= '0';
            ex1_store_data <= (others => '0');
        elsif rising_edge(clk) then
            if flush = '1' then
                ex1_valid <= '0';
            else
                ex1_valid  <= d2_s0_valid;
                ex1_rd     <= d2_s0_rd;
                ex1_reg_we <= d2_s0_reg_we;

                if unsigned(d2_s0_opcode) <= 7 then
                    res := alu0_result;
                elsif d2_s0_opcode = OP_ADDI then
                    res := s0_addi_out;
                else
                    res := (others => '0');
                end if;

                if d2_s0_opcode = OP_CALL then
                    res := std_logic_vector(unsigned(d2_s0_pc) + 4);
                end if;

                ex1_result     <= res;
                ex1_addr       <= std_logic_vector(unsigned(d2_s0_rs1_val) + unsigned(d2_s0_imm));
                ex1_store_data <= d2_s0_rs2_val;
                ex1_mem_req    <= '1' when (d2_s0_opcode = OP_LDW or d2_s0_opcode = OP_STW) else '0';
                ex1_mem_we     <= '1' when d2_s0_opcode = OP_STW else '0';
            end if;
        end if;
    end process;

    -- ─── Branch resolution (from D2 combinational -> flush in EX1) ─
    branch_taken <= d2_s0_valid and (
        ('1' when d2_s0_opcode = OP_BZ  and d2_s0_rs1_val = x"00000000" else '0') or
        ('1' when d2_s0_opcode = OP_BNZ and d2_s0_rs1_val /= x"00000000" else '0') or
        ('1' when d2_s0_opcode = OP_JMP  else '0') or
        ('1' when d2_s0_opcode = OP_CALL else '0') or
        ('1' when d2_s0_opcode = OP_RET  else '0'));
    br_target <= d2_s0_rs1_val when d2_s0_opcode = OP_RET else
                 std_logic_vector(unsigned(d2_s0_pc) + 4 + unsigned(d2_s0_imm));
    next_pc <= br_target;

    flush <= branch_taken;

    -- ─── EX2 stage (slot 1 execute + memory) ─────────────────
    process(clk, rst_n)
        variable res : std_logic_vector(31 downto 0);
    begin
        if rst_n = '0' then
            ex2_s0_rd        <= (others => '0');
            ex2_s0_result    <= (others => '0');
            ex2_s0_reg_we    <= '0';
            ex2_s1_valid     <= '0';
            ex2_s1_rd        <= (others => '0');
            ex2_s1_result    <= (others => '0');
            ex2_s1_reg_we    <= '0';
            ex2_s1_addr      <= (others => '0');
            ex2_s1_mem_req   <= '0';
            ex2_s1_mem_we    <= '0';
            ex2_s1_store_data <= (others => '0');
        elsif rising_edge(clk) then
            -- Slot 0 passes through for WB
            ex2_s0_rd     <= ex1_rd;
            ex2_s0_result <= ex1_result;
            ex2_s0_reg_we <= ex1_reg_we;

            -- Slot 1 execute
            ex2_s1_valid <= d2_paired and d2_s1_valid;
            ex2_s1_rd    <= d2_s1_rd;
            if unsigned(d2_s1_opcode) <= 7 then
                res := alu1_result;
            elsif d2_s1_opcode = OP_ADDI then
                res := s1_addi_out;
            else
                res := (others => '0');
            end if;
            if d2_s1_opcode = OP_CALL then
                res := std_logic_vector(unsigned(d2_s1_pc) + 4);
            end if;
            ex2_s1_result     <= res;
            ex2_s1_reg_we     <= d2_s1_reg_we;
            ex2_s1_addr       <= std_logic_vector(unsigned(d2_s1_rs1_val) + unsigned(d2_s1_imm));
            ex2_s1_mem_req    <= '1' when (d2_s1_opcode = OP_LDW or d2_s1_opcode = OP_STW) else '0';
            ex2_s1_mem_we     <= '1' when d2_s1_opcode = OP_STW else '0';
            ex2_s1_store_data <= d2_s1_rs2_val;
        end if;
    end process;

    -- ─── BTB write (on resolution in EX1/EX2) ────────────────
    process(clk)
        variable idx : integer range 0 to 15;
    begin
        if rising_edge(clk) then
            if branch_taken = '1' then
                idx := to_integer(unsigned(d2_s0_pc(5 downto 2)));
                btb(idx).valid  <= '1';
                btb(idx).tag    <= d2_s0_pc(31 downto 6);
                btb(idx).target <= br_target;
            end if;
        end if;
    end process;

    -- ─── M stage ─────────────────────────────────────────────
    process(clk, rst_n)
    begin
        if rst_n = '0' then
            m_s0_rd     <= (others => '0');
            m_s0_result <= (others => '0');
            m_s0_reg_we <= '0';
            m_s1_rd     <= (others => '0');
            m_s1_result <= (others => '0');
            m_s1_reg_we <= '0';
        elsif rising_edge(clk) then
            m_s0_rd     <= ex2_s0_rd;
            m_s0_reg_we <= ex2_s0_reg_we;
            if ex1_mem_req = '1' and ex1_mem_we = '0' then
                m_s0_result <= dmem_rdata;
            else
                m_s0_result <= ex2_s0_result;
            end if;
            m_s1_rd     <= ex2_s1_rd;
            m_s1_result <= ex2_s1_result;
            m_s1_reg_we <= ex2_s1_reg_we;
        end if;
    end process;

    -- ─── WB stage ────────────────────────────────────────────
    process(clk, rst_n)
    begin
        if rst_n = '0' then
            wb_s0_rd     <= (others => '0');
            wb_s0_result <= (others => '0');
            wb_s0_reg_we <= '0';
            wb_s1_rd     <= (others => '0');
            wb_s1_result <= (others => '0');
            wb_s1_reg_we <= '0';
        elsif rising_edge(clk) then
            wb_s0_rd     <= m_s0_rd;
            wb_s0_result <= m_s0_result;
            wb_s0_reg_we <= m_s0_reg_we;
            wb_s1_rd     <= m_s1_rd;
            wb_s1_result <= m_s1_result;
            wb_s1_reg_we <= m_s1_reg_we;
        end if;
    end process;

    -- ─── Memory port ─────────────────────────────────────────
    dmem_addr  <= ex1_addr;
    dmem_req   <= ex1_mem_req;
    dmem_we    <= ex1_mem_we;
    dmem_wdata <= ex1_store_data;

end architecture rtl;
