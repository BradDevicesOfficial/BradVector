-- SPDX-License-Identifier: MIT
-- BradISA V1 -- VHDL Testbench

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.bradisa_pkg.all;

entity tb_brad_core is
end entity tb_brad_core;

architecture sim of tb_brad_core is

    signal clk       : std_logic := '0';
    signal rst_n     : std_logic := '0';
    signal imem_addr : std_logic_vector(31 downto 0);
    signal imem_rdata : std_logic_vector(31 downto 0);
    signal dmem_addr : std_logic_vector(31 downto 0);
    signal dmem_req  : std_logic;
    signal dmem_we   : std_logic;
    signal dmem_wdata : std_logic_vector(31 downto 0);
    signal dmem_rdata : std_logic_vector(31 downto 0);

    type mem_t is array (0 to 255) of std_logic_vector(31 downto 0);
    signal imem : mem_t := (others => (others => '0'));
    signal dmem : mem_t := (others => (others => '0'));

    constant PERIOD : time := 10 ns;

begin

    -- DUT
    dut: entity work.brad_core
        port map (
            clk => clk, rst_n => rst_n,
            imem_addr => imem_addr, imem_rdata => imem_rdata,
            dmem_addr => dmem_addr, dmem_req => dmem_req,
            dmem_we => dmem_we, dmem_wdata => dmem_wdata,
            dmem_rdata => dmem_rdata
        );

    imem_rdata <= imem(to_integer(unsigned(imem_addr(9 downto 2))));
    dmem_rdata <= dmem(to_integer(unsigned(dmem_addr(9 downto 2)))) when dmem_req = '1' else (others => '0');

    process(dmem_req, dmem_we, dmem_addr, dmem_wdata) begin
        if rising_edge(dmem_req) and dmem_we = '1' then
            dmem(to_integer(unsigned(dmem_addr(9 downto 2)))) <= dmem_wdata;
        end if;
    end process;

    -- Clock
    clk <= not clk after PERIOD/2;

    process begin
        -- Load program
        imem(0) <= x"81100064";  -- ADDI r1, r1, 100
        imem(1) <= x"82200000";  -- ADDI r2, r2, 0
        imem(2) <= x"8110FFFF";  -- ADDI r1, r1, -1  (loop:)
        imem(3) <= x"82200001";  -- ADDI r2, r2, 1
        imem(4) <= x"C010FFF0";  -- BNZ  r1, loop     (-4 words)
        imem(5) <= x"F0000000";  -- RET

        rst_n <= '0';
        wait for 15 ns;
        rst_n <= '1';

        wait for 5000 ns;

        report "Simulation complete" severity note;
        wait;
    end process;

end architecture sim;
