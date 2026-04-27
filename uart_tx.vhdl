library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity uart_tx is
    Generic (
        CLK_FREQ  : integer := 100_000_000;
        BAUD_RATE : integer := 115_200
    );
    Port (
        clk       : in  std_logic;
        rst       : in  std_logic;
        data_in   : in  std_logic_vector(7 downto 0);
        tx_start  : in  std_logic;
        tx        : out std_logic;
        tx_busy   : out std_logic
    );
end uart_tx;

architecture Behavioral of uart_tx is
    constant CLKS_PER_BIT : integer := CLK_FREQ / BAUD_RATE;

    type state_t is (IDLE, START_BIT, DATA_BITS, STOP_BIT, CLEANUP);
    signal state : state_t := IDLE;

    signal clk_count : integer range 0 to CLKS_PER_BIT-1 := 0;
    signal bit_index : integer range 0 to 7 := 0;
    signal tx_data   : std_logic_vector(7 downto 0) := (others => '0');
    signal tx_reg    : std_logic := '1';
begin

    TX_PROC : process(clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                state     <= IDLE;
                clk_count <= 0;
                bit_index <= 0;
                tx_reg    <= '1';
                tx_busy   <= '0';
            else
                case state is
                    when IDLE =>
                        tx_reg    <= '1';
                        clk_count <= 0;
                        bit_index <= 0;
                        tx_busy   <= '0';
                        if tx_start = '1' then
                            tx_data <= data_in;
                            tx_busy <= '1';
                            state   <= START_BIT;
                        end if;

                    when START_BIT =>
                        tx_reg <= '0';
                        if clk_count < CLKS_PER_BIT - 1 then
                            clk_count <= clk_count + 1;
                        else
                            clk_count <= 0;
                            state     <= DATA_BITS;
                        end if;

                    when DATA_BITS =>
                        tx_reg <= tx_data(bit_index);
                        if clk_count < CLKS_PER_BIT - 1 then
                            clk_count <= clk_count + 1;
                        else
                            clk_count <= 0;
                            if bit_index < 7 then
                                bit_index <= bit_index + 1;
                            else
                                bit_index <= 0;
                                state     <= STOP_BIT;
                            end if;
                        end if;

                    when STOP_BIT =>
                        tx_reg <= '1';
                        if clk_count < CLKS_PER_BIT - 1 then
                            clk_count <= clk_count + 1;
                        else
                            clk_count <= 0;
                            state     <= CLEANUP;
                        end if;

                    when CLEANUP =>
                        tx_busy <= '0';
                        state   <= IDLE;
                end case;
            end if;
        end if;
    end process;

    tx <= tx_reg;
end Behavioral;
