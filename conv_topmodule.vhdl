*** ONLY PART OF THE CODE ***

  library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity conv_top is
    Port (
        clk         : in  std_logic;
        rst         : in  std_logic;
        kernel_sel  : in  std_logic_vector(1 downto 0);
        uart_rx_pin : in  std_logic;
        uart_tx_pin : out std_logic;
        led_out     : out std_logic_vector(7 downto 0)
    );
end conv_top;

architecture Behavioral of conv_top is

    constant IMG_W : integer := 256;

    component uart_rx
        Generic (CLK_FREQ : integer := 100_000_000; BAUD_RATE : integer := 115_200);
        Port (
            clk        : in  std_logic;
            rst        : in  std_logic;
            rx         : in  std_logic;
            data_out   : out std_logic_vector(7 downto 0);
            data_valid : out std_logic
        );
    end component;

    component uart_tx
        Generic (CLK_FREQ : integer := 100_000_000; BAUD_RATE : integer := 115_200);
        Port (
            clk      : in  std_logic;
            rst      : in  std_logic;
            data_in  : in  std_logic_vector(7 downto 0);
            tx_start : in  std_logic;
            tx       : out std_logic;
            tx_busy  : out std_logic
        );
    end component;

    type line_t is array (0 to IMG_W-1) of std_logic_vector(7 downto 0);
    signal lineA : line_t := (others => (others => '0'));
    signal lineB : line_t := (others => (others => '0'));
    signal lineC : line_t := (others => (others => '0'));

    signal top_sel  : integer range 0 to 2 := 0;
    signal mid_sel  : integer range 0 to 2 := 1;
    signal bot_sel  : integer range 0 to 2 := 2;

    signal out_buf : line_t := (others => (others => '0'));

    signal rx_data    : std_logic_vector(7 downto 0);
    signal rx_valid   : std_logic;
    signal tx_data    : std_logic_vector(7 downto 0) := (others => '0');
    signal tx_start   : std_logic := '0';
    signal tx_busy    : std_logic;

    -- TX bug fix: tx_busy pickup bekleme
    signal tx_wait_busy : std_logic := '0';

    signal col_count  : integer range 0 to IMG_W := 0;
    signal tx_idx     : integer range 0 to IMG_W := 0;
    signal conv_col   : integer range 0 to IMG_W-1 := 0;

    type state_t is (S_FILL_LINE0, S_FILL_LINE1, S_FILL_LINE2,
                     S_CONV, S_TX, S_NEXT_LINE);
    signal state : state_t := S_FILL_LINE0;

    type kernel_t is array (0 to 8) of integer range -16 to 16;
    signal kernel : kernel_t;
    signal divisor : integer range 1 to 16;

begin

    RX_INST : uart_rx
        port map (clk => clk, rst => rst, rx => uart_rx_pin,
                  data_out => rx_data, data_valid => rx_valid);

    TX_INST : uart_tx
        port map (clk => clk, rst => rst, data_in => tx_data,
                  tx_start => tx_start, tx => uart_tx_pin, tx_busy => tx_busy);

    KERNEL_SELECT : process(kernel_sel)
    begin
        case kernel_sel is
            when "00" =>
                kernel  <= (1, 2, 1,  2, 4, 2,  1, 2, 1);
                divisor <= 16;
            when "01" =>
                kernel  <= (0, -1, 0,  -1, 5, -1,  0, -1, 0);
                divisor <= 1;
            when "10" =>
                kernel  <= (-1, -1, -1,  -1, 8, -1,  -1, -1, -1);
                divisor <= 1;
            when others =>
                kernel  <= (-2, -1, 0,  -1, 1, 1,  0, 1, 2);
                divisor <= 1;
        end case;
    end process;
