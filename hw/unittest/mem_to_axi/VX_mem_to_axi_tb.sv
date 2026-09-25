`include "VX_platform.vh"

module VX_mem_to_axi_tb;
    localparam DATA_WIDTH = 32;
    localparam DATA_SIZE = DATA_WIDTH / 8;

    logic clk;
    logic reset;

    logic                    mem_req_valid [1];
    logic                    mem_req_rw [1];
    logic [DATA_SIZE-1:0]    mem_req_byteen [1];
    logic [15:0]             mem_req_addr [1];
    logic [DATA_WIDTH-1:0]   mem_req_data [1];
    logic [3:0]              mem_req_tag [1];
    wire                     mem_req_ready [1];

    wire                     mem_rsp_valid [1];
    wire [DATA_WIDTH-1:0]    mem_rsp_data [1];
    wire [3:0]               mem_rsp_tag [1];
    logic                    mem_rsp_ready [1];

    wire                     m_axi_awvalid [1];
    logic                    m_axi_awready [1];
    wire [31:0]              m_axi_awaddr [1];
    wire [3:0]               m_axi_awid [1];
    wire [7:0]               m_axi_awlen [1];
    wire [2:0]               m_axi_awsize [1];
    wire [1:0]               m_axi_awburst [1];
    wire [1:0]               m_axi_awlock [1];
    wire [3:0]               m_axi_awcache [1];
    wire [2:0]               m_axi_awprot [1];
    wire [3:0]               m_axi_awqos [1];
    wire [3:0]               m_axi_awregion [1];

    wire                     m_axi_wvalid [1];
    logic                    m_axi_wready [1];
    wire [DATA_WIDTH-1:0]    m_axi_wdata [1];
    wire [DATA_SIZE-1:0]     m_axi_wstrb [1];
    wire                     m_axi_wlast [1];

    logic                    m_axi_bvalid [1];
    wire                     m_axi_bready [1];
    logic [3:0]              m_axi_bid [1];
    logic [1:0]              m_axi_bresp [1];

    wire                     m_axi_arvalid [1];
    logic                    m_axi_arready [1];
    wire [31:0]              m_axi_araddr [1];
    wire [3:0]               m_axi_arid [1];
    wire [7:0]               m_axi_arlen [1];
    wire [2:0]               m_axi_arsize [1];
    wire [1:0]               m_axi_arburst [1];
    wire [1:0]               m_axi_arlock [1];
    wire [3:0]               m_axi_arcache [1];
    wire [2:0]               m_axi_arprot [1];
    wire [3:0]               m_axi_arqos [1];
    wire [3:0]               m_axi_arregion [1];

    logic                    m_axi_rvalid [1];
    wire                     m_axi_rready [1];
    logic [DATA_WIDTH-1:0]   m_axi_rdata [1];
    logic                    m_axi_rlast [1];
    logic [3:0]              m_axi_rid [1];
    logic [1:0]              m_axi_rresp [1];

    VX_mem_to_axi #(
        .DATA_WIDTH      (DATA_WIDTH),
        .ADDR_WIDTH_IN   (16),
        .ADDR_WIDTH_OUT  (32),
        .TAG_WIDTH_IN    (4),
        .TAG_WIDTH_OUT   (4),
        .NUM_PORTS_IN    (1),
        .NUM_BANKS_OUT   (1),
        .TAG_BUFFER_SIZE (4)
    ) dut (
        .clk, .reset,
        .mem_req_valid, .mem_req_rw, .mem_req_byteen, .mem_req_addr,
        .mem_req_data, .mem_req_tag, .mem_req_ready,
        .mem_rsp_valid, .mem_rsp_data, .mem_rsp_tag, .mem_rsp_ready,
        .m_axi_awvalid, .m_axi_awready, .m_axi_awaddr, .m_axi_awid,
        .m_axi_awlen, .m_axi_awsize, .m_axi_awburst, .m_axi_awlock,
        .m_axi_awcache, .m_axi_awprot, .m_axi_awqos, .m_axi_awregion,
        .m_axi_wvalid, .m_axi_wready, .m_axi_wdata, .m_axi_wstrb,
        .m_axi_wlast, .m_axi_bvalid, .m_axi_bready, .m_axi_bid,
        .m_axi_bresp, .m_axi_arvalid, .m_axi_arready, .m_axi_araddr,
        .m_axi_arid, .m_axi_arlen, .m_axi_arsize, .m_axi_arburst,
        .m_axi_arlock, .m_axi_arcache, .m_axi_arprot, .m_axi_arqos,
        .m_axi_arregion, .m_axi_rvalid, .m_axi_rready, .m_axi_rdata,
        .m_axi_rlast, .m_axi_rid, .m_axi_rresp
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    task automatic tick;
        @(posedge clk);
        #1;
    endtask

    task automatic check(input logic condition, input string message);
        if (!condition) begin
            $fatal(1, "%s", message);
        end
    endtask

    task automatic issue_write(input logic [15:0] addr,
                               input logic [31:0] data,
                               input logic [3:0] tag);
        mem_req_valid[0] = 1;
        mem_req_rw[0] = 1;
        mem_req_addr[0] = addr;
        mem_req_data[0] = data;
        mem_req_tag[0] = tag;
        #1;
        check(mem_req_ready[0], "write did not issue without a BRESP");
        check(m_axi_awvalid[0] && m_axi_wvalid[0], "missing AXI write request");
        check(m_axi_awaddr[0] == {14'b0, addr, 2'b0}, "wrong AXI write address");
        check(m_axi_wdata[0] == data, "wrong AXI write data");
        check(m_axi_awid[0] == 0, "writes must share one ordered AXI ID");
        tick();
        mem_req_valid[0] = 0;
    endtask

    initial begin
        reset = 1;
        mem_req_valid[0] = 0;
        mem_req_rw[0] = 0;
        mem_req_byteen[0] = '1;
        mem_req_addr[0] = 0;
        mem_req_data[0] = 0;
        mem_req_tag[0] = 0;
        mem_rsp_ready[0] = 1;
        m_axi_awready[0] = 1;
        m_axi_wready[0] = 1;
        m_axi_bvalid[0] = 0;
        m_axi_bid[0] = 0;
        m_axi_bresp[0] = 0;
        m_axi_arready[0] = 1;
        m_axi_rvalid[0] = 0;
        m_axi_rdata[0] = 0;
        m_axi_rlast[0] = 1;
        m_axi_rid[0] = 0;
        m_axi_rresp[0] = 0;

        repeat (4) tick();
        reset = 0;
        tick();

        issue_write(16'h0010, 32'h11223344, 4'h1);
        issue_write(16'h0020, 32'h55667788, 4'h2);
        issue_write(16'h0030, 32'h99aabbcc, 4'h3);
        issue_write(16'h0040, 32'hddeeff00, 4'h4);

        mem_req_valid[0] = 1;
        mem_req_rw[0] = 1;
        mem_req_addr[0] = 16'h0050;
        mem_req_data[0] = 32'h13579bdf;
        mem_req_tag[0] = 4'h5;
        #1;
        check(!mem_req_ready[0] && !m_axi_awvalid[0] && !m_axi_wvalid[0],
              "write issued beyond the outstanding limit");

        m_axi_bvalid[0] = 1;
        #1;
        check(mem_req_ready[0] && m_axi_awvalid[0] && m_axi_wvalid[0],
              "write slot was not reused on a simultaneous BRESP");
        tick();
        m_axi_bvalid[0] = 0;

        mem_req_valid[0] = 1;
        mem_req_rw[0] = 0;
        mem_req_addr[0] = 16'h0010;
        mem_req_tag[0] = 4'h3;
        #1;
        check(!mem_req_ready[0] && !m_axi_arvalid[0],
              "read bypassed outstanding writes");

        repeat (3) begin
            m_axi_bvalid[0] = 1;
            tick();
            m_axi_bvalid[0] = 0;
            #1;
            check(!mem_req_ready[0] && !m_axi_arvalid[0],
                  "read issued before every write completed");
        end

        m_axi_bvalid[0] = 1;
        tick();
        m_axi_bvalid[0] = 0;
        #1;
        check(mem_req_ready[0] && m_axi_arvalid[0],
              "read did not resume after write drain");
        tick();

        $display("PASSED");
        $finish;
    end
endmodule
