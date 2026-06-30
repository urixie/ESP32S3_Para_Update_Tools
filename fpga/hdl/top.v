`timescale 1ns / 1ps

module top (
    input wire I_CLK20M,          // 20MHz 系统时钟输入

    output  wire O_ESP_UART_RX,   // FPGA -> ESP32 UART RX
    input   wire I_ESP_UART_TX,   // ESP32 UART TX -> FPGA
    input   wire I_OP_RX,         // 光纤RX -> FPGA
    output  wire O_OP_TX         // FPGA -> 光纤TX
);


    assign O_OP_TX = I_ESP_UART_TX;
    assign O_ESP_UART_RX = I_OP_RX;


endmodule