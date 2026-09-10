`include "VX_platform.vh"

module VX_mm_axi_arb_tb #(
    parameter MULTI_OUT = 1
);
    logic clk;
    logic reset = 1;
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    logic [1:0] s_awvalid;
    logic  m_awvalid;
    logic [1:0] s_awready;
    logic  m_awready;
    logic [1:0][31:0] s_awaddr;
    logic [31:0] m_awaddr;
    logic [1:0][3:0] s_awid;
    logic [3:0] m_awid;
    logic [1:0][7:0] s_awlen;
    logic [7:0] m_awlen;
    logic [1:0] s_wvalid;
    logic  m_wvalid;
    logic [1:0] s_wready;
    logic  m_wready;
    logic [1:0][31:0] s_wdata;
    logic [31:0] m_wdata;
    logic [1:0][3:0] s_wstrb;
    logic [3:0] m_wstrb;
    logic [1:0] s_wlast;
    logic  m_wlast;
    logic [1:0] s_bvalid;
    logic  m_bvalid;
    logic [1:0] s_bready;
    logic  m_bready;
    logic [1:0][3:0] s_bid;
    logic [3:0] m_bid;
    logic [1:0][1:0] s_bresp;
    logic [1:0] m_bresp;
    logic [1:0] s_arvalid;
    logic  m_arvalid;
    logic [1:0] s_arready;
    logic  m_arready;
    logic [1:0][31:0] s_araddr;
    logic [31:0] m_araddr;
    logic [1:0][3:0] s_arid;
    logic [3:0] m_arid;
    logic [1:0][7:0] s_arlen;
    logic [7:0] m_arlen;
    logic [1:0] s_rvalid;
    logic  m_rvalid;
    logic [1:0] s_rready;
    logic  m_rready;
    logic [1:0][31:0] s_rdata;
    logic [31:0] m_rdata;
    logic [1:0] s_rlast;
    logic  m_rlast;
    logic [1:0][3:0] s_rid;
    logic [3:0] m_rid;
    logic [1:0][1:0] s_rresp;
    logic [1:0] m_rresp;

    VX_mm_axi_arb #(
        .NUM_INPUTS(2), .NUM_OUTPUTS(1), .ADDR_WIDTH(32),
        .DATA_WIDTH(32), .ID_WIDTH(4), .ARBITER("P"), .STICKY(1),
        .MULTI_OUT(MULTI_OUT)
    ) dut (
        .clk(clk), .reset(reset),
        .s_awvalid(s_awvalid),
        .m_awvalid(m_awvalid),
        .s_awready(s_awready),
        .m_awready(m_awready),
        .s_awaddr(s_awaddr),
        .m_awaddr(m_awaddr),
        .s_awid(s_awid),
        .m_awid(m_awid),
        .s_awlen(s_awlen),
        .m_awlen(m_awlen),
        .s_wvalid(s_wvalid),
        .m_wvalid(m_wvalid),
        .s_wready(s_wready),
        .m_wready(m_wready),
        .s_wdata(s_wdata),
        .m_wdata(m_wdata),
        .s_wstrb(s_wstrb),
        .m_wstrb(m_wstrb),
        .s_wlast(s_wlast),
        .m_wlast(m_wlast),
        .s_bvalid(s_bvalid),
        .m_bvalid(m_bvalid),
        .s_bready(s_bready),
        .m_bready(m_bready),
        .s_bid(s_bid),
        .m_bid(m_bid),
        .s_bresp(s_bresp),
        .m_bresp(m_bresp),
        .s_arvalid(s_arvalid),
        .m_arvalid(m_arvalid),
        .s_arready(s_arready),
        .m_arready(m_arready),
        .s_araddr(s_araddr),
        .m_araddr(m_araddr),
        .s_arid(s_arid),
        .m_arid(m_arid),
        .s_arlen(s_arlen),
        .m_arlen(m_arlen),
        .s_rvalid(s_rvalid),
        .m_rvalid(m_rvalid),
        .s_rready(s_rready),
        .m_rready(m_rready),
        .s_rdata(s_rdata),
        .m_rdata(m_rdata),
        .s_rlast(s_rlast),
        .m_rlast(m_rlast),
        .s_rid(s_rid),
        .m_rid(m_rid),
        .s_rresp(s_rresp),
        .m_rresp(m_rresp)
    );

    bit [15:0] read_pending, write_pending, write_done;
    int read_left[16], write_left[16];
    int read_count, write_count, r_count, b_count;
    int pending_reads, pending_writes;
    int max_reads, max_writes, cycles;
    int ar_stalls, aw_stalls, w_stalls, r_stalls, b_stalls;
    bit write_active;
    logic [3:0] write_tag;
    bit ar_held, aw_held, w_held;
    bit [1:0] r_held, b_held;
    logic [43:0] held_ar, held_aw;
    logic [36:0] held_w;
    logic [1:0][38:0] held_r;
    logic [1:0][5:0] held_b;

    task automatic check(input bit condition, input string message);
        if (!condition) begin
            $fatal(1, "cycle=%0d: %s", cycles, message);
        end
    endtask

    initial begin
        read_pending = '0; write_pending = '0; write_done = '0;
        read_count = 0; write_count = 0; r_count = 0; b_count = 0;
        pending_reads = 0; pending_writes = 0;
        max_reads = 0; max_writes = 0; cycles = 0;
        ar_stalls = 0; aw_stalls = 0; w_stalls = 0; r_stalls = 0; b_stalls = 0;
        write_active = 0;
        ar_held = 0; aw_held = 0; w_held = 0; r_held = '0; b_held = '0;
        forever begin
            @(posedge clk);
            if (!reset) begin
                cycles++;
                check(cycles < 1000, "watchdog");
                if (ar_held) begin
                    check(m_arvalid && {m_araddr, m_arid, m_arlen} == held_ar,
                          "AR changed under backpressure");
                    ar_stalls++;
                end
                if (aw_held) begin
                    check(m_awvalid && {m_awaddr, m_awid, m_awlen} == held_aw,
                          "AW changed under backpressure");
                    aw_stalls++;
                end
                if (w_held) begin
                    check(m_wvalid && {m_wdata, m_wstrb, m_wlast} == held_w,
                          "W changed under backpressure");
                    w_stalls++;
                end
                ar_held = m_arvalid && !m_arready;
                aw_held = m_awvalid && !m_awready;
                w_held = m_wvalid && !m_wready;
                held_ar = {m_araddr, m_arid, m_arlen};
                held_aw = {m_awaddr, m_awid, m_awlen};
                held_w = {m_wdata, m_wstrb, m_wlast};

                for (int i = 0; i < 2; i++) begin
                    if (r_held[i]) begin
                        check(s_rvalid[i] && {s_rdata[i], s_rlast[i], s_rid[i], s_rresp[i]} == held_r[i],
                              "R changed under source backpressure");
                        r_stalls++;
                    end
                    if (b_held[i]) begin
                        check(s_bvalid[i] && {s_bid[i], s_bresp[i]} == held_b[i],
                              "B changed under source backpressure");
                        b_stalls++;
                    end
                    r_held[i] = s_rvalid[i] && !s_rready[i];
                    b_held[i] = s_bvalid[i] && !s_bready[i];
                    held_r[i] = {s_rdata[i], s_rlast[i], s_rid[i], s_rresp[i]};
                    held_b[i] = {s_bid[i], s_bresp[i]};
                end

                if (m_arvalid && m_arready) begin
                    int src;
                    src = s_arready[0] ? 0 : 1;
                    check($onehot(s_arvalid & s_arready), "AR source handshake is not one-hot");
                    check(m_arid == (MULTI_OUT ? {src[0], s_arid[src][2:0]} : s_arid[src]),
                          "AR source ID was lost");
                    check(m_araddr == s_araddr[src] && m_arlen == s_arlen[src], "AR payload mismatch");
                    check(!read_pending[m_arid], "duplicate outstanding read ID");
                    read_pending[m_arid] = 1;
                    read_left[m_arid] = int'(m_arlen) + 1;
                    read_count++;
                    pending_reads++;
                    if (pending_reads > max_reads) begin
                        max_reads = pending_reads;
                    end
                end
                if (m_rvalid) begin
                    int src;
                    src = int'(m_rid[3]);
                    check(read_pending[m_rid], "R for an unknown ID");
                    check(s_rvalid == (2'b01 << src), "R returned to the wrong source");
                    check(s_rid[src] == {1'b0, m_rid[2:0]}, "R did not restore the low ID");
                    check(s_rdata[src] == m_rdata && s_rresp[src] == m_rresp
                          && s_rlast[src] == m_rlast, "R payload mismatch");
                    check(m_rready == s_rready[src], "R ready from the wrong source");
                    if (m_rready) begin
                        check(m_rlast == (read_left[m_rid] == 1), "RLAST at the wrong beat");
                        read_left[m_rid]--;
                        if (m_rlast) begin
                            read_pending[m_rid] = 0;
                            pending_reads--;
                            r_count++;
                        end
                    end
                end

                if (m_awvalid && m_awready) begin
                    int src;
                    src = s_awready[0] ? 0 : 1;
                    check(!write_active, "AW accepted before prior WLAST");
                    check($onehot(s_awvalid & s_awready), "AW source handshake is not one-hot");
                    check(m_awid == (MULTI_OUT ? {src[0], s_awid[src][2:0]} : s_awid[src]),
                          "AW source ID was lost");
                    check(m_awaddr == s_awaddr[src] && m_awlen == s_awlen[src], "AW payload mismatch");
                    check(!write_pending[m_awid], "duplicate outstanding write ID");
                    write_pending[m_awid] = 1;
                    write_done[m_awid] = 0;
                    write_left[m_awid] = int'(m_awlen) + 1;
                    write_tag = m_awid;
                    write_active = 1;
                    write_count++;
                    pending_writes++;
                    if (pending_writes > max_writes) begin
                        max_writes = pending_writes;
                    end
                end
                if (m_wvalid) begin
                    int src;
                    src = int'(write_tag[3]);
                    check(write_active, "W without an accepted AW");
                    check(m_wdata == s_wdata[src] && m_wstrb == s_wstrb[src]
                          && m_wlast == s_wlast[src], "W interleaved from another source");
                    if (m_wready) begin
                        check((s_wready & s_wvalid) == (2'b01 << src), "W source handshake mismatch");
                        check(m_wlast == (write_left[write_tag] == 1), "WLAST at the wrong beat");
                        write_left[write_tag]--;
                        if (m_wlast) begin
                            write_active = 0;
                            write_done[write_tag] = 1;
                        end
                    end
                end
                if (m_bvalid) begin
                    int src;
                    src = int'(m_bid[3]);
                    check(write_pending[m_bid] && write_done[m_bid], "B before complete write");
                    check(s_bvalid == (2'b01 << src), "B returned to the wrong source");
                    check(s_bid[src] == {1'b0, m_bid[2:0]} && s_bresp[src] == m_bresp,
                          "B did not restore the ID/response");
                    check(m_bready == s_bready[src], "B ready from the wrong source");
                    if (m_bready) begin
                        write_pending[m_bid] = 0;
                        pending_writes--;
                        b_count++;
                    end
                end
            end
        end
    end

    task automatic wait_ar(input bit src);
        bit accepted;
        accepted = 0;
        for (int i = 0; i < 12; i++) begin
            @(posedge clk);
            if (s_arready[src]) begin
                accepted = 1;
                break;
            end
        end
        check(accepted, "AR blocked while read responses are withheld");
        @(negedge clk);
        s_arvalid[src] = 0;
    endtask

    task automatic send_ar(input int src, input logic [3:0] id);
        @(negedge clk);
        s_arvalid[src] = 1;
        s_arid[src] = id;
        s_araddr[src] = 32'h1000 + 32'(src * 256) + 32'(id * 16);
        s_arlen[src] = 0;
        wait_ar(src[0]);
    endtask

    task automatic send_r(input logic [3:0] id, input bit last);
        @(negedge clk);
        m_rvalid = 1;
        m_rid = id;
        m_rdata = 32'hcab00000 + 32'(id * 16) + 32'(read_left[id]);
        m_rresp = id[1:0];
        m_rlast = last;
        s_rready = 2'b11 ^ (2'b01 << id[3]);
        repeat (3) @(posedge clk);
        @(negedge clk);
        s_rready = '1;
        @(posedge clk);
        check(m_rready, "R cannot complete at its ready destination");
        @(negedge clk);
        m_rvalid = 0;
    endtask

    task automatic wait_aw(input bit src);
        bit accepted;
        accepted = 0;
        for (int i = 0; i < 12; i++) begin
            @(posedge clk);
            if (s_awready[src]) begin
                accepted = 1;
                break;
            end
        end
        check(accepted, "AW blocked while B responses are withheld");
        @(negedge clk);
        s_awvalid[src] = 0;
    endtask

    task automatic send_aw(input int src, input logic [3:0] id);
        @(negedge clk);
        s_awvalid[src] = 1;
        s_awid[src] = id;
        s_awaddr[src] = 32'h2000 + 32'(src * 256) + 32'(id * 16);
        s_awlen[src] = 0;
        wait_aw(src[0]);
    endtask

    task automatic send_w(input int src, input logic [31:0] data, input bit last);
        @(negedge clk);
        s_wvalid[src] = 1;
        s_wdata[src] = data;
        s_wstrb[src] = last ? 4'h5 : 4'hf;
        s_wlast[src] = last;
        m_wready = 0;
        repeat (3) @(posedge clk);
        @(negedge clk);
        m_wready = 1;
        @(posedge clk);
        check(m_wvalid && s_wready[src], "W cannot complete from its owner");
        @(negedge clk);
        s_wvalid[src] = 0;
    endtask

    task automatic send_b(input logic [3:0] id);
        @(negedge clk);
        m_bvalid = 1;
        m_bid = id;
        m_bresp = id[1:0];
        s_bready = 2'b11 ^ (2'b01 << id[3]);
        repeat (3) @(posedge clk);
        @(negedge clk);
        s_bready = '1;
        @(posedge clk);
        check(m_bready, "B cannot complete at its ready destination");
        @(negedge clk);
        m_bvalid = 0;
    endtask

    initial begin
        s_arvalid = '0; s_araddr = '0; s_arid = '0; s_arlen = '0;
        s_awvalid = '0; s_awaddr = '0; s_awid = '0; s_awlen = '0;
        s_wvalid = '0; s_wdata = '0; s_wstrb = '0; s_wlast = '0;
        s_rready = '1; s_bready = '1;
        m_arready = 0; m_awready = 0; m_wready = 0;
        m_rvalid = 0; m_rdata = '0; m_rid = '0; m_rresp = '0; m_rlast = 0;
        m_bvalid = 0; m_bid = '0; m_bresp = '0;
        repeat (4) @(posedge clk);
        @(negedge clk);
        reset = 0;

        if (!MULTI_OUT) begin
            m_arready = 1;
            send_ar(1, 1);
            send_ar(0, 1);
            check(0, "single-outstanding negative control unexpectedly accepted two reads");
        end

        // A later higher-priority source must not replace a stalled CP request.
        s_arvalid[1] = 1; s_arid[1] = 1; s_araddr[1] = 32'h1100; s_arlen[1] = 1;
        repeat (2) @(posedge clk);
        @(negedge clk);
        s_arvalid[0] = 1; s_arid[0] = 1; s_araddr[0] = 32'h1000; s_arlen[0] = 1;
        repeat (3) @(posedge clk);
        @(negedge clk);
        m_arready = 1;
        wait_ar(1);
        wait_ar(0);
        send_ar(1, 3); send_ar(0, 3); send_ar(1, 7); send_ar(0, 7);
        check(read_count == 6 && pending_reads == 6 && r_count == 0,
              "did not reach six read transactions before any R response");
        send_r(15, 1); send_r(3, 1); send_r(9, 0); send_r(7, 1);
        send_r(9, 1); send_r(1, 0); send_r(11, 1); send_r(1, 1);

        s_awvalid[1] = 1; s_awid[1] = 1; s_awaddr[1] = 32'h2100; s_awlen[1] = 1;
        repeat (2) @(posedge clk);
        @(negedge clk);
        s_awvalid[0] = 1; s_awid[0] = 1; s_awaddr[0] = 32'h2000; s_awlen[0] = 1;
        repeat (3) @(posedge clk);
        @(negedge clk);
        m_awready = 1;
        wait_aw(1);
        send_w(1, 32'hbb001001, 0); send_w(1, 32'hbb001002, 1);
        wait_aw(0);
        send_w(0, 32'haa001001, 0); send_w(0, 32'haa001002, 1);
        send_aw(1, 3); send_w(1, 32'hbb003001, 1);
        send_aw(0, 3); send_w(0, 32'haa003001, 1);
        send_aw(1, 7); send_w(1, 32'hbb007001, 1);
        send_aw(0, 7); send_w(0, 32'haa007001, 1);
        check(write_count == 6 && pending_writes == 6 && b_count == 0,
              "did not reach six writes before any B response");
        send_b(15); send_b(3); send_b(9); send_b(7); send_b(11); send_b(1);

        check(read_pending == 0 && write_pending == 0 && r_count == 6 && b_count == 6,
              "missing or duplicate response");
        check(max_reads == 6 && max_writes == 6, "multi-outstanding coverage missing");
        check(ar_stalls > 0 && aw_stalls > 0 && w_stalls > 0 && r_stalls > 0 && b_stalls > 0,
              "backpressure coverage missing");
        $display("PASSED: reads=%0d writes=%0d max_read_pending=%0d max_write_pending=%0d stalls AR/AW/W/R/B=%0d/%0d/%0d/%0d/%0d",
                 r_count, b_count, max_reads, max_writes, ar_stalls, aw_stalls, w_stalls, r_stalls, b_stalls);
        $finish;
    end
endmodule
