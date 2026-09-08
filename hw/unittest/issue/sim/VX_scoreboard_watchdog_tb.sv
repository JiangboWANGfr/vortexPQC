`include "VX_define.vh"

module VX_scoreboard_watchdog_tb;
    import VX_gpu_pkg::*;
    localparam BASE_BUDGET = 100000;
    logic clk = 0;
    logic reset = 1;
    always #5 clk = ~clk;

    VX_ibuffer_if ibuffer_if [PER_ISSUE_WARPS]();
    VX_writeback_if writeback_if();
    VX_scoreboard_if scoreboard_if();
    logic [NUM_EX_UNITS-1:0] fu_release;
    VX_scoreboard dut (
        .clk(clk), .reset(reset), .fu_release(fu_release),
        .ibuffer_if(ibuffer_if), .writeback_if(writeback_if),
        .scoreboard_if(scoreboard_if)
    );

    int mode, cycle, issued, accepted;
    int pending [NUM_EX_UNITS];
    int sent [PER_ISSUE_WARPS];
    logic [PER_ISSUE_WARPS-1:0] sampled_fire;
    wire [PER_ISSUE_WARPS-1:0] input_fire;
    for (genvar w = 0; w < PER_ISSUE_WARPS; ++w) begin : g_inputs
        assign input_fire[w] = ibuffer_if[w].valid && ibuffer_if[w].ready;
        always_comb begin
            ibuffer_if[w].valid = !reset && ((mode == 3) ? (w < 2)
                : (sent[w] == 0 || (mode == 2 && w == 0)));
            ibuffer_if[w].data = '0;
            ibuffer_if[w].data.ex_type = EX_BITS'((mode == 2 && w == 0) ? EX_ALU : EX_SFU);
            ibuffer_if[w].data.PC = PC_BITS'(w + 1);
            ibuffer_if[w].data.tmask = '1;
            ibuffer_if[w].data.fu_lock = 1;
            ibuffer_if[w].data.fu_unlock = 1;
            if (mode == 3 && w == 0) begin
                ibuffer_if[w].data.wb = (sent[w] == 0);
                ibuffer_if[w].data.rd = NUM_REGS_BITS'(1);
                ibuffer_if[w].data.used_rs = NUM_SRC_OPDS'(sent[w] != 0);
                ibuffer_if[w].data.rs1 = NUM_REGS_BITS'(1);
            end
        end
    end

    initial begin
        if (!$value$plusargs("mode=%d", mode)) mode = 0;
        cycle = 0; issued = 0; accepted = 0;
        foreach (pending[e]) pending[e] = 0;
        foreach (sent[w]) sent[w] = 0;
        fu_release = '0;
        writeback_if.valid = 0;
        writeback_if.data = '0;
        scoreboard_if.ready = 1;
        repeat (4) @(negedge clk);
        reset = 0;
        for (cycle = 0; cycle < 8 * STALL_TIMEOUT; ++cycle) begin
            fu_release = '0;
            for (int e = 0; e < NUM_EX_UNITS; ++e) begin
                if (pending[e] != 0 && (mode == 3
                    || (mode == 0 && cycle % (BASE_BUDGET / 2) == 0)
                    || (mode == 2 && e == EX_ALU)))
                    fu_release[e] = 1;
            end
            @(posedge clk);
            sampled_fire = input_fire;
            for (int e = 0; e < NUM_EX_UNITS; ++e) begin
                if (fu_release[e]) begin
                    --pending[e];
                    ++accepted;
                end
            end
            if (scoreboard_if.valid && scoreboard_if.ready) begin
                ++pending[scoreboard_if.data.ex_type];
                ++issued;
            end
            @(negedge clk);
            for (int w = 0; w < PER_ISSUE_WARPS; ++w)
                if (sampled_fire[w]) ++sent[w];
            if (mode == 0 && accepted == PER_ISSUE_WARPS) begin
                if (issued != PER_ISSUE_WARPS || cycle <= BASE_BUDGET)
                    $fatal(1, "test did not exercise a long queue");
                $display("PASS: %0d operations accepted over %0d cycles", accepted, cycle);
                $finish;
            end
        end
        $fatal(1, "watchdog test incomplete: mode=%0d issued=%0d accepted=%0d", mode, issued, accepted);
    end
endmodule
