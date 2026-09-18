`include "VX_define.vh"

module VX_pqc_nttmul_d_tb;
    import VX_gpu_pkg::*;

    localparam L = `VX_CFG_NUM_ALU_LANES;
    localparam signed [31:0] Q = 32'sd8380417;
    logic clk = 0;
    logic reset = 1;
    always #5 clk = ~clk;

    VX_execute_if #(.data_t(alu_execute_t)) execute_if();
    VX_result_if #(.data_t(alu_result_t)) result_if();
    VX_pqc_nttmul #(.NUM_LANES(L)) dut (
        .clk(clk), .reset(reset), .execute_if(execute_if), .result_if(result_if)
    );

    function automatic logic signed [31:0] coefficient(input int seed);
        case (seed % 8)
            0: return -8 * Q;
            1: return  8 * Q;
            2: return -Q;
            3: return  Q;
            4: return -1;
            5: return  0;
            6: return  1;
            default: return 32'(seed * 345679 - 1234567);
        endcase
    endfunction

    function automatic logic signed [31:0] twiddle(input int seed);
        case (seed % 8)
            0: return -41978;
            1: return  41978;
            2: return -(Q / 2);
            3: return  Q / 2;
            4: return -1;
            5: return  0;
            6: return  1;
            default: return 32'(seed * 1237 % Q) - Q / 2;
        endcase
    endfunction

    function automatic logic signed [31:0] mont_d(
        input logic signed [31:0] a,
        input logic signed [31:0] b
    );
        logic signed [63:0] product;
        logic [31:0] inverted;
        logic signed [31:0] factor;
        logic signed [63:0] difference;
        begin
            product = a * b;
            inverted = product[31:0] * 32'd58728449;
            factor = $signed(inverted);
            difference = product - 64'(factor) * Q;
            return 32'(difference >>> 32);
        end
    endfunction

    task automatic check_case(input int mode, input int stage, input int vector);
        logic signed [31:0] expected [L];
        logic signed [31:0] value [L];
        logic signed [31:0] zeta [L];
        int distance;
        begin
            distance = 1 << stage;
            for (int lane = 0; lane < L; ++lane) begin
                value[lane] = coefficient(vector * 37 + lane);
                zeta[lane] = twiddle(vector * 19 + lane);
            end
            if (mode == 0) begin
                for (int lane = 0; lane < L; ++lane)
                    expected[lane] = mont_d(value[lane], zeta[lane]);
            end else begin
                for (int lane = 0; lane < L; ++lane) begin
                    int high;
                    logic signed [31:0] a, b, product;
                    if ((lane & distance) != 0) continue;
                    high = lane + distance;
                    a = value[lane];
                    b = value[high];
                    if (mode == 1) begin
                        product = mont_d(b, zeta[lane]);
                        expected[lane] = a + product;
                        expected[high] = a - product;
                    end else begin
                        product = mont_d(a - b, zeta[lane]);
                        expected[lane] = a + b;
                        expected[high] = product;
                    end
                end
            end

            @(negedge clk);
            execute_if.data = '0;
            execute_if.data.header.tmask = '1;
            execute_if.data.header.uuid = UUID_WIDTH'(vector);
            execute_if.data.op_type = mode == 0 ? INST_ALU_NTTMUL_K : INST_ALU_NTTBF_K;
            execute_if.data.op_args.alu.imm20 = mode == 0
                ? 20'h10 : 20'(16 + ((mode == 2) ? 8 : 0) + stage);
            for (int lane = 0; lane < L; ++lane) begin
                execute_if.data.rs1_data[lane] = `VX_CFG_XLEN'(value[lane]);
                execute_if.data.rs2_data[lane] = `VX_CFG_XLEN'(zeta[lane]);
            end
            execute_if.valid = 1;
            do @(posedge clk); while (!execute_if.ready);
            @(negedge clk);
            execute_if.valid = 0;
            wait (result_if.valid);
            #1;
            if (result_if.data.header.uuid != UUID_WIDTH'(vector))
                $fatal(1, "D header mismatch vector=%0d", vector);
            for (int lane = 0; lane < L; ++lane)
                if (result_if.data.data[lane] !== `VX_CFG_XLEN'(expected[lane]))
                    $fatal(1, "D mode=%0d stage=%0d vector=%0d lane=%0d got=%h want=%h",
                           mode, stage, vector, lane,
                           result_if.data.data[lane], expected[lane]);
            repeat (3) @(posedge clk);
            @(negedge clk);
            result_if.ready = 1;
            @(posedge clk);
            @(negedge clk);
            result_if.ready = 0;
        end
    endtask

    initial begin
        if (L != 32) $fatal(1, "D test requires 32 lanes");
        execute_if.valid = 0;
        execute_if.data = '0;
        result_if.ready = 0;
        repeat (4) @(negedge clk);
        reset = 0;
        for (int vector = 0; vector < 8; ++vector)
            check_case(0, 0, vector);
        for (int mode = 1; mode <= 2; ++mode)
            for (int stage = 0; stage < 5; ++stage)
                for (int vector = 0; vector < 8; ++vector)
                    check_case(mode, stage, vector);
        $display("NTTMUL.D/NTTBF.D PASSED vectors=88 lanes=%0d", L);
        $finish;
    end
endmodule
