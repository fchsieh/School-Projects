import Clocks :: *;
import Vector::*;
import FIFO::*;
import BRAM::*;
import BRAMFIFO::*;
import Uart::*;
import Sdram::*;

interface HwMainIfc;
	method ActionValue#(Bit#(8)) serial_tx;
	method Action serial_rx(Bit#(8) rx);
endinterface

module mkHwMain#(Ulx3sSdramUserIfc mem) (HwMainIfc);
	Clock curclk <- exposeCurrentClock;
	Reset currst <- exposeCurrentReset;

	Reg#(Bit#(32)) cycles <- mkReg(0);
	Reg#(Bit#(32)) cycleOutputStart <- mkReg(0);
	rule incCyclecount;
		cycles <= cycles + 1;
	endrule

	Reg#(Bit#(32)) cycleBegin <- mkReg(0);

	FIFO#(Bit#(8)) serialrxQ <- mkFIFO;
	FIFO#(Bit#(8)) serialtxQ <- mkFIFO;

	FIFO#(Bit#(8)) rowbufferQ1 <- mkSizedBRAMFIFO(512);
	FIFO#(Bit#(8)) rowbufferQ2 <- mkSizedBRAMFIFO(512);

	// for combining the two filter results
	Reg#(Bit#(32)) total_processed <- mkReg(0);

	// zero-padding pixel
	Bit#(8) zero_padding = 0;

	Reg#(Bit#(32)) recv_pixels <- mkReg(0);

	// For horizontal filter
	FIFO#(Bit#(8)) hz_filter_stream <- mkSizedBRAMFIFO(512);
	FIFO#(Bit#(8)) hz_filter_result <- mkSizedBRAMFIFO(512);
	Reg#(Bit#(32)) hz_relayed <- mkReg(0);

	// For vertical filter
	FIFO#(Bit#(8)) vt_filter <- mkSizedBRAMFIFO(512);
	Vector#(3, Reg#(Bit#(8))) vt_filter_buffer <- replicateM(mkReg(0));
	// output of the vertical filter
	Reg#(Bool) vt_corner_done <- mkReg(False);
	Reg#(Bool) vt_filter_done <- mkReg(False);
	Reg#(Bool) vt_filter_init <- mkReg(False);
	Reg#(Bool) vt_last_row_init <- mkReg(False);
	Reg#(Bool) vt_start_processing <- mkReg(False);
	FIFO#(Bit#(8)) vt_filter_result <- mkSizedBRAMFIFO(512);
	Reg#(Bit#(32)) vt_last_row_cnt <- mkReg(0);
	FIFO#(Bit#(8)) before_last_row <- mkSizedBRAMFIFO(512);

	// buffer and streams
	FIFO#(Bit#(8)) hz_first_buffer <- mkSizedBRAMFIFO(512);
	FIFO#(Bit#(8)) hz_second_buffer <- mkSizedBRAMFIFO(512);
	FIFO#(Bit#(8)) hz_third_buffer <- mkSizedBRAMFIFO(512);
	FIFO#(Bit#(8)) hz_first_stream <- mkSizedBRAMFIFO(512);
	FIFO#(Bit#(8)) hz_second_stream <- mkSizedBRAMFIFO(512);
	FIFO#(Bit#(8)) hz_third_stream <- mkSizedBRAMFIFO(512);
	Reg#(Bit#(32)) hz_filter_processed <- mkReg(0);
	Reg#(Bool) hz_relay_init <- mkReg(False);

	function Bit#(8) convolve (Bit#(8) first, Bit#(8) second);
		Int#(8) first_num = unpack(first);
		Int#(8) second_num = unpack(second);
		Int#(8) result = first_num - second_num;
		Bit#(8) result_output = pack(result);
		return result_output;
	endfunction

	rule relayRow1;
		// received a new pixel
		recv_pixels <= recv_pixels + 1;

		// get pixel from input
		serialrxQ.deq;
		let pix = serialrxQ.first;

		// send to hz filter buffer
		vt_filter_buffer[0] <= pix;

		// for processing vertical filter in last row
		if (recv_pixels >= 512*254 && recv_pixels < 512*255) begin
			before_last_row.enq(pix);
		end
		
		// send to row buffer for next row
		rowbufferQ1.enq(pix);
	endrule

	rule relayRow2;
		// get pixel from row buffer
		rowbufferQ1.deq;
		let pix = rowbufferQ1.first;

		// send to hz filter buffer
		vt_filter_buffer[1] <= pix;

		// send to row buffer for next row
		rowbufferQ2.enq(pix);
		hz_filter_stream.enq(pix);
	endrule

	rule relayRow3;
		// get pixel from row buffer
		rowbufferQ2.deq;
		let pix = rowbufferQ2.first;

		// send to hz filter buffer
		vt_filter_buffer[2] <= pix;

		// send to vertical filter buffer, since we need to know which pixel to start from
		vt_filter.enq(pix);
	endrule

	rule verticalFilterFirstRow (recv_pixels >= 512 && recv_pixels < 1025 && !vt_corner_done);
		vt_start_processing <= True;
		let actual_cnt = recv_pixels - 512 - 1;
		if (actual_cnt == 0 && !vt_filter_init) begin
			let prev = zero_padding;
			let curr = vt_filter_buffer[0];
			let conv = convolve(curr, prev);
			//$write("#%d, [%d, %d] => %d | First Row | Vertical\n", actual_cnt, prev, curr, conv);
			vt_filter_result.enq(conv);
			vt_filter_init <= True;
		end else if (actual_cnt > 0 && actual_cnt <= 511) begin
			let prev = zero_padding;
			let curr = vt_filter_buffer[0];
			let conv = convolve(curr, prev);
			//$write("#%d, [%d, %d] => %d | First Row | Vertical\n", actual_cnt, prev, curr, conv);
			vt_filter_result.enq(conv);
		end
	endrule

	rule verticalFilterLastRow (vt_corner_done && vt_last_row_cnt < 1024 && vt_last_row_cnt >= 0 && recv_pixels == 512*256);
		if (vt_last_row_cnt == 0 && !vt_last_row_init) begin
			let actual_cnt = vt_last_row_cnt + 512*255;
			before_last_row.deq;
			let prev = before_last_row.first;
			let curr = zero_padding;
			let conv = convolve(curr, prev);
			//$write("#%d, [%d, %d] => %d | Last Row | Vertical\n", actual_cnt, prev, curr, conv);
			vt_filter_result.enq(conv);
			vt_last_row_cnt <= vt_last_row_cnt + 1;
			vt_last_row_init <= True;
		end else if (vt_last_row_cnt > 0 && vt_last_row_cnt < 1024) begin
			let actual_cnt = vt_last_row_cnt + 512*255;
			before_last_row.deq;
			let prev = before_last_row.first;
			let curr = zero_padding;
			let conv = convolve(curr, prev);
			//$write("#%d, [%d, %d] => %d | Last Row | Vertical\n", actual_cnt, prev, curr, conv);
			vt_filter_result.enq(conv);
			vt_last_row_cnt <= vt_last_row_cnt + 1;
		end
	endrule

	rule verticalFilterEndCorner (recv_pixels == 512*256 && !vt_corner_done);
		let actual_cnt = recv_pixels - 1 - 512;
		vt_filter.deq;
		let prev = vt_filter.first;
		let curr = vt_filter_buffer[0];
		let conv = convolve(curr, prev);
		//$write("#%d, [%d, %d] => %d | End corner | Vertical\n", actual_cnt, prev, curr, conv);
		vt_filter_result.enq(conv);
		vt_corner_done <= True;	
	endrule

	rule verticalFilter (recv_pixels >= 1025 && recv_pixels < 512*256 && !vt_corner_done && !vt_filter_done);
		let actual_cnt = recv_pixels - 512 - 1;
		vt_filter.deq;
		let prev = vt_filter.first;
		let curr = vt_filter_buffer[0];
		let conv = convolve(curr, prev);
		//$write("#%d, [%d, %d] => %d | Vertical\n", actual_cnt, prev, curr, conv);
		vt_filter_result.enq(conv);
	endrule

	rule hzFilterRelay (hz_relayed < 512 * 256);
		// duplicate pixel to three-col buffer
		hz_filter_stream.deq;
		let recv = hz_filter_stream.first;

		hz_first_buffer.enq(recv);
		hz_second_buffer.enq(recv);
		hz_third_buffer.enq(recv);
		hz_relayed <= hz_relayed + 1;
	endrule

	rule hzFilterFirstRelay (hz_relayed > 0);
		if (hz_relayed <= 512*256) begin
			hz_first_buffer.deq;
			let recv = hz_first_buffer.first;
			hz_first_stream.enq(recv);
		end else begin
			hz_first_stream.enq(zero_padding);
		end
	endrule

	rule hzFilterSecondRelay (hz_relayed > 0);
		if (hz_relayed > 1) begin
			hz_second_buffer.deq;
			let recv = hz_second_buffer.first;
			hz_second_stream.enq(recv);
		end else begin
			hz_second_stream.enq(zero_padding);
		end
	endrule

	rule hzFilterThirdRelay (hz_relayed > 0);
		if (hz_relayed > 2) begin
			hz_third_buffer.deq;
			let recv = hz_third_buffer.first;
			hz_third_stream.enq(recv);
		end else begin
			hz_third_stream.enq(zero_padding);
		end
	endrule

	rule horizontalFilter (hz_relayed > 1);
		let actual_cnt = hz_filter_processed - 1;

		if (hz_filter_processed == 0) begin
			// remove first buffer
			hz_first_stream.deq;
			hz_second_stream.deq;
			hz_third_stream.deq;
		end else if (hz_filter_processed == 512*256) begin // end of the processing
			hz_third_stream.deq;
			let first_col = hz_third_stream.first;
			hz_second_stream.deq;
			let second_col = hz_second_stream.first;
			let third_col = zero_padding;
			
			let conv = convolve(third_col, first_col);
			//$write("#%d, [%d, %d, %d] => %d | Horizontal\n", actual_cnt, first_col, second_col, third_col, conv);
			hz_filter_result.enq(conv);
		end else begin
			// buffers before the end of the processing
			// need to check whether it is a start of a new row or an end of a row
			// or neither of them
			if (actual_cnt % 512 == 0) begin // start of a row
				hz_first_stream.deq;
				let first_col = zero_padding; // change first-col to zero padding
				hz_second_stream.deq;
				let second_col = hz_second_stream.first;
				hz_third_stream.deq;
				let third_col = hz_first_stream.first;

				let conv = convolve(third_col, first_col);
				//$write("#%d, [%d, %d, %d] => %d | Horizontal\n", actual_cnt, first_col, second_col, third_col, conv);
				hz_filter_result.enq(conv);
			end else if (actual_cnt % 512 == 511) begin // end of a row
				hz_first_stream.deq;
				let first_col = hz_third_stream.first;
				hz_second_stream.deq;
				let second_col = hz_second_stream.first;
				hz_third_stream.deq;
				let third_col = zero_padding; // change third-col to zero padding
				
				let conv = convolve(third_col, first_col);
				//$write("#%d, [%d, %d, %d] => %d | Horizontal\n", actual_cnt, first_col, second_col, third_col, conv);
				hz_filter_result.enq(conv);
			end else begin
				hz_first_stream.deq;
				let first_col = hz_third_stream.first;
				hz_second_stream.deq;
				let second_col = hz_second_stream.first;
				hz_third_stream.deq;
				let third_col = hz_first_stream.first;
				
				let conv = convolve(third_col, first_col);
				//$write("#%d, [%d, %d, %d] => %d | Horizontal\n", actual_cnt, first_col, second_col, third_col, conv);
				hz_filter_result.enq(conv);
			end
		end

		hz_filter_processed <= hz_filter_processed + 1;
	endrule

	rule combinedResult;
		hz_filter_result.deq;
		Bit#(8) hz = hz_filter_result.first;
		vt_filter_result.deq;
		Bit#(8) vt = vt_filter_result.first;

		Int#(8) hz_num = unpack(hz);
		Int#(8) vt_num = unpack(vt);
		Int#(8) hz_vt_sum = hz_num + vt_num;
		
		Int#(8) res_num = hz_vt_sum / 2;
		
		if (hz_vt_sum < 0) begin
			Bit#(8) res = zero_padding;
			serialtxQ.enq(res);
			$write("#%d, (h = %d, v = %d) => sum = %d, avg = %d | Output\n", total_processed, hz_num, vt_num, hz_vt_sum, res_num);
			
		end else begin
			Bit#(8) res = pack(res_num);
			serialtxQ.enq(res);
			$write("#%d, (h = %d, v = %d) => sum = %d, avg = %d | Output\n", total_processed, hz_num, vt_num, hz_vt_sum, res_num);
		end
		total_processed <= total_processed + 1;
		
	endrule

	Reg#(Bit#(32)) pixOutCnt <- mkReg(0);
	method ActionValue#(Bit#(8)) serial_tx;
		if ( cycleOutputStart == 0 ) begin
			$write( "Image processing latency: %d cycles\n", cycles - cycleBegin );
			cycleOutputStart <= cycles;
		end
		if ( pixOutCnt + 1 >= 512*256 ) begin
			$write( "Image processing total cycles: %d\n", cycles - cycleBegin );
		end
		pixOutCnt <= pixOutCnt + 1;
		serialtxQ.deq;
		return serialtxQ.first();
	endmethod
	method Action serial_rx(Bit#(8) d);
		if ( cycleBegin == 0 ) cycleBegin <= cycles;
		serialrxQ.enq(d);
	endmethod
endmodule