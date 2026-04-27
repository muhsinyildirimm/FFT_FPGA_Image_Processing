library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity uart_rx is
    Generic (
        CLK_FREQ  : integer := 100_000_000;  -- 100 MHz
        BAUD_RATE : integer := 115_200
    );
    Port (
        clk       : in  std_logic;
        rst       : in  std_logic;
        rx        : in  std_logic;
        data_out  : out std_logic_vector(7 downto 0);
        data_valid: out std_logic
    );
end uart_rx;

architecture Behavioral of uart_rx is
    constant CLKS_PER_BIT : integer := CLK_FREQ / BAUD_RATE;  -- 868
    
    type state_t is (IDLE, START_BIT, DATA_BITS, STOP_BIT, CLEANUP);
    signal state : state_t := IDLE;
    
    signal clk_count : integer range 0 to CLKS_PER_BIT-1 := 0;
    signal bit_index : integer range 0 to 7 := 0;
    signal rx_data   : std_logic_vector(7 downto 0) := (others => '0');
    signal rx_sync1, rx_sync2 : std_logic := '1';
begin

    -- Metastabilite onlemek icin 2 flip-flop senkronizasyonu
    SYNC_PROC : process(clk)
    begin
        if rising_edge(clk) then
            rx_sync1 <= rx;
            rx_sync2 <= rx_sync1;
        end if;
    end process;

    RX_PROC : process(clk)
    begin
        if rising_edge(clk) then
            if rst = '1' then
                state      <= IDLE;
                clk_count  <= 0;
                bit_index  <= 0;
                data_valid <= '0';
            else
                case state is
                    when IDLE =>
                        data_valid <= '0';
                        clk_count  <= 0;
                        bit_index  <= 0;
                        if rx_sync2 = '0' then  -- Start bit algilandi
                            state <= START_BIT;
                        end if;

                    when START_BIT =>
                        if clk_count = CLKS_PER_BIT / 2 then
                            if rx_sync2 = '0' then
                                clk_count <= 0;
                                state     <= DATA_BITS;
                            else
                                state <= IDLE;
                            end if;
                        else
                            clk_count <= clk_count + 1;
                        end if;

                    when DATA_BITS =>
                        if clk_count < CLKS_PER_BIT - 1 then
                            clk_count <= clk_count + 1;
                        else
                            clk_count <= 0;
                            rx_data(bit_index) <= rx_sync2;
                            if bit_index < 7 then
                                bit_index <= bit_index + 1;
                            else
                                bit_index <= 0;
                                state     <= STOP_BIT;
                            end if;
                        end if;

                    when STOP_BIT =>
                        if clk_count < CLKS_PER_BIT - 1 then
                            clk_count <= clk_count + 1;
                        else
                            data_valid <= '1';
                            clk_count  <= 0;
                            state      <= CLEANUP;
                        end if;

                    when CLEANUP =>
                        data_valid <= '0';
                        state      <= IDLE;
                end case;
            end if;
        end if;
    end process;

    data_out <= rx_data;
end Behavioral;
