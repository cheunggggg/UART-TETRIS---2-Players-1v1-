## Clock
set_property PACKAGE_PIN W5 [get_ports sys_clock]
set_property IOSTANDARD LVCMOS33 [get_ports sys_clock]
create_clock -add -name sys_clk_pin -period 10.00 -waveform {0 5} [get_ports sys_clock]

## Reset
set_property PACKAGE_PIN U17 [get_ports reset]
set_property IOSTANDARD LVCMOS33 [get_ports reset]

## USB UART (Player 1 - PuTTY keyboard)
set_property PACKAGE_PIN B18 [get_ports usb_uart_rxd]
set_property IOSTANDARD LVCMOS33 [get_ports usb_uart_rxd]
set_property PACKAGE_PIN A18 [get_ports usb_uart_txd]
set_property IOSTANDARD LVCMOS33 [get_ports usb_uart_txd]

## PMOD JB - BT2 (Player 2 - 8Bitdo Zero 2)
set_property PACKAGE_PIN A14 [get_ports {jb[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[0]}]
set_property PACKAGE_PIN A16 [get_ports {jb[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[1]}]
set_property PACKAGE_PIN B15 [get_ports {jb[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[2]}]
set_property PACKAGE_PIN B16 [get_ports {jb[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[3]}]
set_property PACKAGE_PIN A15 [get_ports {jb[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[4]}]
set_property PACKAGE_PIN A17 [get_ports {jb[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[5]}]
set_property PACKAGE_PIN C15 [get_ports {jb[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[6]}]
set_property PACKAGE_PIN C16 [get_ports {jb[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {jb[7]}]

## PMOD JA - KYPD
set_property PACKAGE_PIN B13 [get_ports {ja[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[0]}]
set_property PACKAGE_PIN F14 [get_ports {ja[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[1]}]
set_property PACKAGE_PIN D17 [get_ports {ja[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[2]}]
set_property PACKAGE_PIN E17 [get_ports {ja[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[3]}]
set_property PACKAGE_PIN G13 [get_ports {ja[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[4]}]
set_property PACKAGE_PIN C17 [get_ports {ja[5]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[5]}]
set_property PACKAGE_PIN D18 [get_ports {ja[6]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[6]}]
set_property PACKAGE_PIN E18 [get_ports {ja[7]}]
set_property IOSTANDARD LVCMOS33 [get_ports {ja[7]}]


#Push Buttons
set_property PACKAGE_PIN U18 [get_ports {push_buttons_4bits_tri_i[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {push_buttons_4bits_tri_i[0]}]
set_property PACKAGE_PIN T18 [get_ports {push_buttons_4bits_tri_i[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {push_buttons_4bits_tri_i[1]}]
set_property PACKAGE_PIN W19 [get_ports {push_buttons_4bits_tri_i[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {push_buttons_4bits_tri_i[2]}]
set_property PACKAGE_PIN T17 [get_ports {push_buttons_4bits_tri_i[3]}]
set_property IOSTANDARD LVCMOS33 [get_ports {push_buttons_4bits_tri_i[3]}]
set_property PACKAGE_PIN W17 [get_ports {push_buttons_4bits_tri_i[4]}]
set_property IOSTANDARD LVCMOS33 [get_ports {push_buttons_4bits_tri_i[4]}]

#Switches
set_property PACKAGE_PIN V17 [get_ports {dip_switches_16bits_tri_i[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dip_switches_16bits_tri_i[0]}]
set_property PACKAGE_PIN V16 [get_ports {dip_switches_16bits_tri_i[1]}]
set_property IOSTANDARD LVCMOS33 [get_ports {dip_switches_16bits_tri_i[1]}]