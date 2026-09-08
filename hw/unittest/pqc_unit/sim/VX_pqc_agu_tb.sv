`include "VX_define.vh"

module VX_pqc_agu_tb;
    import VX_gpu_pkg::*;
    `include "kat.vh"

    localparam M = `VX_CFG_NUM_LSU_LANES;
    localparam B = LSU_WORD_SIZE * 8;
    localparam W = 1600 / B;
    localparam L = `VX_CFG_NUM_SFU_LANES;
    localparam BASE = 32'h10000;

    logic clk = 0;
    logic reset = 1;
    always #5 clk = ~clk;

    VX_execute_if #(.data_t(sfu_execute_t)) execute_if();
    VX_result_if #(.data_t(sfu_result_t)) result_if();
    VX_lsu_sched_if client_if();
    logic [`VX_CFG_NUM_WARPS-1:0] acquired;
    wire unlock_req;
    wire [NW_WIDTH-1:0] unlock_wid;
    logic unlock_ack;

    VX_pqc_agu dut (
        .clk(clk), .reset(reset), .execute_if(execute_if),
        .result_if(result_if), .client_if(client_if),
        .warp_pending_alm_empty(acquired),
        .unlock_req(unlock_req), .unlock_wid(unlock_wid), .unlock_ack(unlock_ack)
    );

    logic [1599:0] memory [L];
    logic [1599:0] expected [L];
    lsu_rsp_data_t pending[$];
    int total_cycles = 0;

    task automatic run_case(input logic [L-1:0] mask,
                            input bit eop, input int order);
        int reads, writes, requests, results, unlocks, done_age;
        int active, response_words, cycle;
        bit response_batch;
        lsu_rsp_data_t queued, response;
        reads = 0; writes = 0; requests = 0;
        results = 0; unlocks = 0; done_age = 0;
        active = $countones(mask);
        response_words = 0; response_batch = 0;
        pending.delete();
        for (int l = 0; l < L; ++l) begin
            memory[l] = (l % 2 == 0) ? KAT_ZERO_IN : KAT_SEED_IN;
            expected[l] = !mask[l] ? memory[l]
                        : ((l % 2 == 0) ? KAT_ZERO_OUT1 : KAT_SEED_OUT1);
        end

        @(negedge clk);
        execute_if.data = '0;
        execute_if.data.op_type = INST_SFU_PQC;
        execute_if.data.header.wid = NW_WIDTH'(1);
        execute_if.data.header.tmask = mask;
        execute_if.data.header.eop = eop;
        for (int l = 0; l < L; ++l)
            execute_if.data.rs1_data[l] = `VX_CFG_XLEN'(BASE + l * 4096);
        execute_if.valid = 1;
        acquired = '0;
        result_if.ready = 0;
        unlock_ack = 0;
        client_if.req_ready = 0;
        client_if.rsp_valid = 0;
        client_if.rsp_data = '0;
        @(posedge clk);
        if (!execute_if.ready) $fatal(1, "AGU not ready for next packet");
        @(negedge clk);
        execute_if.valid = 0;

        for (cycle = 0; cycle < 10000; ++cycle) begin
            // Request stalls and sparse, reversed responses are independent.
            client_if.req_ready = (cycle % 5 != 1);
            if (cycle == 7) acquired = '1;
            if (result_if.valid || unlock_req || done_age != 0) ++done_age;
            result_if.ready = (order != 1 || done_age >= 7);
            unlock_ack = (order != 0 || done_age >= 7);

            if (!client_if.rsp_valid && response_batch && cycle % 3 != 1) begin
                int q, first_lane;
                q = int'(pending.size()) - 1;
                response = pending[q];
                response.mask = '0;
                first_lane = -1;
                for (int i = M - 1; i >= 0; --i) begin
                    if (pending[q].mask[i] && (first_lane < 0 || i % 2 == first_lane % 2)) begin
                        response.mask[i] = 1;
                        pending[q].mask[i] = 0;
                        first_lane = i;
                    end
                end
                if (pending[q].mask == '0) queued = pending.pop_back();
                client_if.rsp_data = response;
                client_if.rsp_valid = 1;
            end

            @(posedge clk);
            if (client_if.req_valid && cycle < 7)
                $fatal(1, "memory request before acquire");
            if (client_if.req_valid && client_if.req_ready) begin
                queued = '0;
                queued.tag = client_if.req_data.tag;
                queued.mask = client_if.req_data.mask;
                ++requests;
                for (int i = 0; i < M; ++i) begin
                    if (client_if.req_data.mask[i]) begin
                        int byte_addr, l, w;
                        byte_addr = int'(client_if.req_data.addr[i]) * LSU_WORD_SIZE;
                        l = (byte_addr - BASE) / 4096;
                        w = ((byte_addr - BASE) % 4096) / LSU_WORD_SIZE;
                        if (l < 0 || l >= L || w < 0 || w >= W || !mask[l])
                            $fatal(1, "request outside active state");
                        if (w != int'(client_if.req_data.tag) + i)
                            $fatal(1, "tag/address mismatch");
                        if (client_if.req_data.byteen[i] != {LSU_WORD_SIZE{1'b1}})
                            $fatal(1, "partial state word");
                        if (client_if.req_data.rw) begin
                            if (client_if.req_data.data[i] !== expected[l][w*B +: B])
                                $fatal(1, "lane %0d word %0d got %h expected %h",
                                    l, w, client_if.req_data.data[i], expected[l][w*B +: B]);
                            memory[l][w*B +: B] = client_if.req_data.data[i];
                            ++writes;
                        end else begin
                            queued.data[i] = memory[l][w*B +: B];
                            ++reads;
                        end
                    end
                end
                if (!client_if.req_data.rw) begin
                    pending.push_back(queued);
                    if (reads % W == 0) response_batch = 1;
                end
            end
            if (client_if.rsp_valid && client_if.rsp_ready) begin
                response_words += $countones(client_if.rsp_data.mask);
                if (response_words % W == 0) begin
                    if (pending.size() != 0) $fatal(1, "response accounting mismatch");
                    response_batch = 0;
                end
                // Deassert on the falling edge after the DUT consumes this beat.
                @(negedge clk);
                client_if.rsp_valid = 0;
            end else begin
                if (result_if.valid && result_if.ready) begin
                    ++results;
                    if (results != 1 || result_if.data.header !== execute_if.data.header)
                        $fatal(1, "duplicate or incorrect result");
                end
                if (unlock_req && unlock_ack) begin
                    ++unlocks;
                    if (!eop || unlocks != 1 || unlock_wid != NW_WIDTH'(1))
                        $fatal(1, "early or duplicate unlock");
                end
                @(negedge clk);
            end
            if (results == 1 && unlocks == int'(eop) && execute_if.ready) break;
        end
        if (cycle == 10000) $fatal(1, "completion timeout");
        if (reads != 2 * active * W || writes != active * W
         || requests != 3 * active * ((W + M - 1) / M))
            $fatal(1, "traffic count mismatch reads=%0d writes=%0d requests=%0d", reads, writes, requests);
        for (int l = 0; l < L; ++l)
            if (memory[l] !== expected[l]) $fatal(1, "final state mismatch");
        // A late duplicate completion must also fail.
        repeat (3) begin
            @(posedge clk);
            if (result_if.valid || unlock_req) $fatal(1, "late completion");
            @(negedge clk);
        end
        total_cycles += cycle;
        $display("AGU mask=%h eop=%0d order=%0d cycles=%0d PASS", mask, eop, order, cycle);
    endtask

    initial begin
        execute_if.valid = 0;
        execute_if.data = '0;
        result_if.ready = 0;
        acquired = '0;
        unlock_ack = 0;
        client_if.req_ready = 0;
        client_if.rsp_valid = 0;
        client_if.rsp_data = '0;
        repeat (4) @(negedge clk);
        reset = 0;
        run_case('1, 1, 0);
        run_case(L'(2), 1, 1);
        run_case(L'(1), 1, 2);
        run_case(L'(1), 0, 0);
        run_case(L'(2), 1, 0);
        run_case('0, 0, 2);
        run_case('0, 1, 0);
        $display("AGU PASSED XLEN=%0d MEM_LANES=%0d total_cycles=%0d", `VX_CFG_XLEN, M, total_cycles);
        $finish;
    end
endmodule
