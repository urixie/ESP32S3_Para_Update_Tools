`timescale 1ns / 1ps

module top (
    // ======================== 时钟与指示灯 ========================
    input wire I_CLK20M,          // 20MHz 系统时钟输入

    // ======================== ADS7886 ADC 接口 ========================
    input  wire I_ADCS7886_SDO,   // ADS7886 串行数据输入
    output wire O_ADCS7886_CLK,   // ADS7886 SPI CLK
    output wire O_ADCS7886_CS,    // ADS7886 片选信号

    // ======================== ADS7882 ADC 接口 ========================
    input  wire [11:0] I_ADCS7882_DATA,    // ADS7882 并行数据输入 DB11~DB0
    output wire        O_ADCS7882_CONVST,  // ADS7882 CONVST

    // ======================== ESP32 SPI 从机接口 ========================
    input  wire I_ESP_SPI_SCLK,   // ESP 发送的 SPI 时钟
    input  wire I_ESP_SPI_MOSI,   // ESP 发送的数据，当前 fpga_spi_ram_slave 未使用
    output wire O_ESP_SPI_MISO,   // ESP 接收的数据 MISO

    // ======================== 控制与状态信号 ========================
    input  wire I_ESP_START_ADS7886,          // 上升沿启动一帧采集，RAM缓存 ADS7886
    input  wire I_ESP_START_ADS7882,  // 上升沿启动一帧采集，RAM缓存 ADS7882

    output wire O_ESP_DATA_READY      // 数据就绪信号，高电平表示一帧采集完成
);





endmodule