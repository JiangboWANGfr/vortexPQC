`include "VX_define.vh"

module VX_pqc_sfu_tb;
    import VX_gpu_pkg::*;
    import VX_rtu_pkg::*;
    `include "kat.vh"

    localparam M = `VX_CFG_NUM_LSU_LANES;
    localparam B = LSU_WORD_SIZE * 8;
    localparam W = 1600 / B;
    localparam BASE = 32'h10000;

    logic clk = 0;
    logic reset = 1;
    always #5 clk = ~clk;

    VX_dispatch_if dispatch_if [`VX_CFG_ISSUE_WIDTH]();
    VX_commit_if commit_if [`VX_CFG_ISSUE_WIDTH]();
    VX_sched_csr_if sched_csr_if();
    VX_dcr_csr_if dcr_csr_if();
    VX_warp_ctl_if warp_ctl_if();
    VX_sched_unlock_if unlock_if();
    VX_lsu_sched_if memory_if();
    VX_rtu_bus_if #(.NUM_LANES(`VX_CFG_NUM_THREADS), .TAG_WIDTH(RTU_REQ_TAG_WIDTH)) rtu_if();

    VX_sfu_unit dut (
        .clk(clk), .reset(reset), .dispatch_if(dispatch_if), .commit_if(commit_if),
        .sched_csr_if(sched_csr_if), .dcr_csr_if(dcr_csr_if),
        .warp_ctl_if(warp_ctl_if), .sched_unlock_if(unlock_if),
        .pqc_mem_if(memory_if), .rtu_bus_if(rtu_if)
    );

    assign sched_csr_if.cycles = '0;
    assign sched_csr_if.instret = '0;
    assign sched_csr_if.active_warps = '1;
    assign sched_csr_if.thread_masks = '1;
    assign sched_csr_if.mscratch = '0;
    assign sched_csr_if.cta_csrs = '0;
    assign sched_csr_if.cta_lane = '0;
    assign sched_csr_if.csr_mstatus = '0;
    assign sched_csr_if.csr_mtvec = '0;
    assign sched_csr_if.csr_mepc = '0;
    assign sched_csr_if.csr_mcause = '0;
    assign sched_csr_if.csr_mtval = '0;
    assign dcr_csr_if.value = '0;
    assign dcr_csr_if.ready = 1;
    assign warp_ctl_if.warp_pending_alm_empty = '1;
    assign warp_ctl_if.lsu_sched_drained = 1;
    assign warp_ctl_if.dvstack_ptr = '0;
    assign warp_ctl_if.bar_phase = 0;
    assign memory_if.req_ready = 1;
    assign rtu_if.arm_ready = 1;
    assign rtu_if.req_ready = 1;

    logic [1599:0] memory;
    int response_words, writes, ties, commits[4], unlocks[4], status_sent;
    int cycle, tie_cycle, backpressure;

    // The final drain response and RTU status arrive together. Both real units
    // then request the production SFU unlock port on the following cycle.
    assign rtu_if.win_valid = (memory_if.rsp_valid && memory_if.rsp_ready
                          && response_words + $countones(memory_if.rsp_data.mask) == 2 * W)
                          || (status_sent > 0 && status_sent < 3);
    always_comb begin
        rtu_if.win_data = '0;
        rtu_if.win_data.wid = NW_WIDTH'((status_sent == 0) ? 0 : status_sent + 1);
        rtu_if.win_data.slot = RTU_SLOT_BITS'(`VX_RT_STATUS);
        rtu_if.win_data.mask = 1;
        rtu_if.win_data.data[0] = 32'h42 + 32'(rtu_if.win_data.wid);
    end

    assign commit_if[0].ready = (backpressure == 0) || (tie_cycle >= 0 && cycle >= tie_cycle + backpressure);

    always @(posedge clk) begin
        if (reset) begin
            memory <= KAT_ZERO_IN;
            memory_if.rsp_valid <= 0;
            memory_if.rsp_data <= '0;
            response_words <= 0;
            status_sent <= 0;
            writes = 0;
            ties = 0;
            cycle <= 0;
            tie_cycle <= -1;
            for (int i = 0; i < 4; ++i) begin
                commits[i] = 0;
                unlocks[i] = 0;
            end
        end else begin
            cycle <= cycle + 1;
            if (memory_if.rsp_valid && !memory_if.rsp_ready)
                $fatal(1, "unexpected response backpressure");
            if (memory_if.rsp_valid && memory_if.rsp_ready)
                response_words <= response_words + $countones(memory_if.rsp_data.mask);
            memory_if.rsp_valid <= 0;
            if (memory_if.req_valid && memory_if.req_ready) begin
                memory_if.rsp_data <= '0;
                memory_if.rsp_data.tag <= memory_if.req_data.tag;
                memory_if.rsp_data.mask <= memory_if.req_data.mask;
                memory_if.rsp_valid <= !memory_if.req_data.rw;
                for (int i = 0; i < M; ++i) begin
                    if (memory_if.req_data.mask[i]) begin
                        int w;
                        w = (int'(memory_if.req_data.addr[i]) * LSU_WORD_SIZE - BASE) / LSU_WORD_SIZE;
                        if (w < 0 || w >= W) $fatal(1, "address outside state");
                        if (memory_if.req_data.rw) begin
                            memory[w*B +: B] <= memory_if.req_data.data[i];
                            ++writes;
                        end else begin
                            memory_if.rsp_data.data[i] <= memory[w*B +: B];
                        end
                    end
                end
            end
            if (rtu_if.win_valid && !rtu_if.win_ready) $fatal(1, "RTU status rejected");
            if (rtu_if.win_valid && rtu_if.win_ready) status_sent <= status_sent + 1;
            if (dut.pqc_unlock_req && dut.rtu_unlock_if.valid) begin
                ++ties;
                if (tie_cycle < 0) tie_cycle <= cycle;
                if (dut.pqc_unlock_ack || !unlock_if.valid || unlock_if.wid != dut.rtu_unlock_if.wid)
                    $fatal(1, "RTU/PQC unlock priority violated");
            end
            if (unlock_if.valid) begin
                if (int'(unlock_if.wid) > 3) $fatal(1, "wrong warp unlocked");
                ++unlocks[int'(unlock_if.wid)];
                if (unlocks[int'(unlock_if.wid)] != 1) $fatal(1, "duplicate warp unlock");
            end
            if (commit_if[0].valid && commit_if[0].ready) begin
                int wid;
                wid = int'(commit_if[0].data.wid);
                if (wid > 3) $fatal(1, "wrong warp committed");
                ++commits[wid];
                if (commits[wid] != 1) $fatal(1, "duplicate result for warp %0d", wid);
                if (commit_if[0].data.tmask != 1 || !commit_if[0].data.eop)
                    $fatal(1, "incorrect result header");
                if (wid != 1 && commit_if[0].data.data[0] != `VX_CFG_XLEN'(32'h42 + wid))
                    $fatal(1, "RTU status corrupted");
            end
        end
    end

    task automatic dispatch(input int wid, input bit pqc);
        @(negedge clk);
        dispatch_if[0].data = '0;
        dispatch_if[0].data.wis = ISSUE_WIS_W'(wid);
        dispatch_if[0].data.tmask = 1;
        dispatch_if[0].data.sop = 1;
        dispatch_if[0].data.eop = 1;
        dispatch_if[0].data.op_type = pqc ? INST_SFU_PQC : INST_SFU_RTUW;
        dispatch_if[0].data.op_args.rtuw.op = RTUW_OP_WAIT;
        dispatch_if[0].data.rs1_data[0] = `VX_CFG_XLEN'(BASE);
        dispatch_if[0].valid = 1;
        do @(posedge clk); while (!dispatch_if[0].ready);
        @(negedge clk);
        dispatch_if[0].valid = 0;
    endtask

    initial begin
        dispatch_if[0].valid = 0;
        dispatch_if[0].data = '0;
        backpressure = 0;
        for (int test_id = 0; test_id < 2; ++test_id) begin
            @(negedge clk);
            reset = 1;
            backpressure = test_id * 16;
            repeat (4) @(negedge clk);
            reset = 0;
            dispatch(0, 0);
            dispatch(2, 0);
            dispatch(3, 0);
            dispatch(1, 1);
            wait (commits[0] == 1 && commits[1] == 1 && commits[2] == 1 && commits[3] == 1);
            repeat (20) @(negedge clk);
            if (ties < 2) $fatal(1, "missing sustained completion contention");
            for (int i = 0; i < 4; ++i)
                if (unlocks[i] != 1) $fatal(1, "missing warp unlock");
            if (memory !== KAT_ZERO_OUT1 || writes != W || response_words != 2 * W)
                $fatal(1, "PQC state/traffic mismatch");
            $display("SFU RTU/PQC tie PASS: backpressure=%0d ties=%0d commits=1/1/1/1 unlocks=1/1/1/1", backpressure, ties);
        end
        $finish;
    end

    initial begin
        #200000;
        $fatal(1, "SFU contention test timed out");
    end
endmodule
