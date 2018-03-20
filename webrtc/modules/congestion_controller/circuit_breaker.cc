/*
*  WEBRTC-CB
*/

#include "modules/congestion_controller/circuit_breaker.h"

#include "common_types.h"  // NOLINT(build/include)
#include "modules/rtp_rtcp/source/time_util.h"
#include <cmath>
#include <array>
#include "rtc_base/logging.h"


namespace webrtc {
	CircuitBreaker::CircuitBreaker(RTPSender* rtp_sender, Clock* clock) :
		rtp_sender_(rtp_sender),
		clock_(clock),
		codec_("unknown"),
        sending_(true),
		previous_report_blocks_(new ReportBlockInfoMap()),
		media_timeout_map_(new MediaTimeoutMap())
	{
		RTC_LOG(LS_INFO) << "Circuit Breaker created.";
		//previous_report_blocks_ = new ReportBlockInfoMap();
		// Initial media framing interval set to inter-frame interval for standard 24 fps high-def video
        media_framing_interval_ = 1000/24;
		// Initial rtcp interval set to half of the default interval defined in the WebRTC library
		rtcp_interval_ = RTCP_INTERVAL_VIDEO_MS / 2; 
		RTC_LOG(LS_INFO) << "Initial RTCP Interval value: " << rtcp_interval_;
		// Initial estimate of the rtcp interval at the receiver also set to half of the default interval
		rtcp_interval_sender_estimate_ = RTCP_INTERVAL_VIDEO_MS / 2;
		RTC_LOG(LS_INFO) << "Initial Sender Estimate of RTCP Interval value: " << rtcp_interval_sender_estimate_;
		// Initial RTT set to a sensible guess value of 100 ms
		uint64_t initial_rtt = 100;
		RTC_LOG(LS_INFO) << "Initial RTT value: " << initial_rtt;
		// Initial CB Interval value valid for all newly seen SSRCs calculated from initial values
		initial_cb_interval_ = static_cast<uint32_t>(ceil(3 * fmin(fmax(fmax(10 * frame_group_size_ * media_framing_interval_, 10 * initial_rtt),
			3 * rtcp_interval_sender_estimate_), fmax(15, 3 * rtcp_interval_)) / (3 * rtcp_interval_sender_estimate_)));
		RTC_LOG(LS_INFO) << "Initial calculated CB_INTERVAL value: " << initial_cb_interval_;
	}

	CircuitBreaker::~CircuitBreaker() {
		delete previous_report_blocks_;
		delete media_timeout_map_;
	}

	void CircuitBreaker::OnReceivedRtcpReportBlocks(const ReportBlockList& report_blocks) {
		uint32_t send_time_ntp;
		for (const RTCPReportBlock& report_block : report_blocks) {								// For each report block
			send_time_ntp = report_block.last_sender_report_timestamp;							// Get the last SR timestamp
			if (send_time_ntp != 0) {															// If 0 abort as calculations will be ineffective
				int64_t rtt_new = 0;															// Var to hold the RTT value calc. from this rep. block
				uint32_t delay_ntp = report_block.delay_since_last_sender_report;				// Get the DSLR
				uint32_t receive_time_ntp = CompactNtp(clock_->CurrentNtpTime());				// Get the current local NTP time.
				uint32_t rtt_ntp = receive_time_ntp - delay_ntp - send_time_ntp;				// Get the RTT in 1/(2^16) seconds.
				rtt_new = CompactNtpRttToMs(rtt_ntp);											// Convert RTT to 1/1000 seconds (milliseconds).
				if (previous_report_blocks_->find(report_block.sender_ssrc) ==					// If first time receiving from this SSRC
													previous_report_blocks_->end()) {
					MediaTimeoutStruct media_timeout_struct;									// Prepare a media timeout struct
					media_timeout_struct.media_timeout = 
						ceil(NON_REPORTING_THRESHOLD * fmax(fmax(media_framing_interval_, rtt_new), rtcp_interval_sender_estimate_) / rtcp_interval_sender_estimate_);
					media_timeout_struct.media_timeout_counter = 0;
					CbIntervalRttSmoothedPair cb_rtt =											// Prepare the structures to be inserted 
						CbIntervalRttSmoothedPair(initial_cb_interval_, rtt_new);				// in the map of prev. info blocks
					CbIntrvlRttReportBlockListPair cb_rtt_rep_block_list =						// to represent this SSRC
						CbIntrvlRttReportBlockListPair(cb_rtt, ReportBlockInfoList());
					previous_report_blocks_->insert(											// Insert into the map
						std::pair<uint32_t, CbIntrvlRttReportBlockListPair>(report_block.sender_ssrc, cb_rtt_rep_block_list));
                    RTC_LOG(LS_INFO) << "New RTT value of: " << rtt_new << " for SSRC " << report_block.sender_ssrc;
				}
				else {																			// Else if SSRC already present
					// If received report block indicates non-increasing highest sequence number
					// Increment media timeout received packets counter & if media timeout packets have been received trigger the CB 
					// Either way recalculate the media timeout for this SSRC and if larger than before, replace the old value 
					if (report_block.extended_highest_sequence_number <= (*media_timeout_map_)[report_block.sender_ssrc].extended_highest_sequence_number) {
						(*media_timeout_map_)[report_block.sender_ssrc].media_timeout_counter++;
						if ((*media_timeout_map_)[report_block.sender_ssrc].media_timeout_counter == (*media_timeout_map_)[report_block.sender_ssrc].media_timeout) {
							HandleTriggeredCircuitBreaker(kMediaTimeout);
						}
						uint32_t new_media_timeout = CalculateMediaTimeout(report_block.sender_ssrc);
						if (new_media_timeout > (*media_timeout_map_)[report_block.sender_ssrc].media_timeout) {
							(*media_timeout_map_)[report_block.sender_ssrc].media_timeout = new_media_timeout;
							RTC_LOG(LS_INFO) << "Media Timeout for SSRC: " << report_block.sender_ssrc << " updated to: " << (*media_timeout_map_)[report_block.sender_ssrc].media_timeout;
						}
					}
					// Else if the highest sequence number has increased
					// Set the counter to 0 and if not sending set the media timeout to 0
					// If sending recalculate the media timeout
					else {
						(*media_timeout_map_)[report_block.sender_ssrc].media_timeout_counter = 0;
						(*media_timeout_map_)[report_block.sender_ssrc].media_timeout = sending_ ? CalculateMediaTimeout(report_block.sender_ssrc) : 0;
					}
					// In both cases update the highest sequence number value
					(*media_timeout_map_)[report_block.sender_ssrc].extended_highest_sequence_number 
							= report_block.extended_highest_sequence_number;
					(*previous_report_blocks_)[report_block.sender_ssrc].first.second =			// Blend the new RTT value with the total RTT
						(0.8*(*previous_report_blocks_)[report_block.sender_ssrc].first.second) + (0.2*rtt_new);
                    RTC_LOG(LS_INFO) << "New RTT value of: " << (*previous_report_blocks_)[report_block.sender_ssrc].first.second 
						<< " for SSRC " << report_block.sender_ssrc;
				}
				ReportBlockInfo report_block_info;											// Prepare the new report block info record
				report_block_info.dlsr = report_block.delay_since_last_sender_report;		
				report_block_info.fraction_lost = report_block.fraction_lost;
                RTC_LOG(LS_INFO) << "New fraction lost seen: " << static_cast<float>(report_block.fraction_lost) / (1 << 8);
                (*previous_report_blocks_)[report_block.sender_ssrc].second					// Add it to the records
					.push_back(report_block_info);	
				if ((*previous_report_blocks_)[report_block.sender_ssrc].second.size() >	// If more than CB_INT packets received from this SSRC
					(*previous_report_blocks_)[report_block.sender_ssrc].first.first) {		
					ReportBlockInfoList rep_block_info_list_for_last_interval =				// Grab the report block info list for 
						(*previous_report_blocks_)[report_block.sender_ssrc].second;		// this circuit breaker interval
					float avg_fraction_lost = 0;											// Calculate the average fraction lost
					int count = 0;														    // for this circuit breaker interval
					for (const ReportBlockInfo& info : rep_block_info_list_for_last_interval) {	
						avg_fraction_lost += static_cast<float>(info.fraction_lost) / (1 << 8);
                        RTC_LOG(LS_INFO) << "New fraction lost added to average: " << static_cast<float>(info.fraction_lost) / (1 << 8);
						count++;
					}
                    (*previous_report_blocks_)[report_block.sender_ssrc].second.clear();	// Clear the list from the records
					avg_fraction_lost /= count;
					float loss_event_rate = avg_fraction_lost / 256.0;						// Format the average fraction lost to fit the loss event rate
                    RTC_LOG(LS_INFO) << "Loss Event rate: " << loss_event_rate << " calculated from Average Fraction Lost of: " << avg_fraction_lost;
					uint8_t num_packets_per_ack = 1;										// b = 1 (RFC 8083 Section 3)
					uint64_t tcp_rate = packet_size_ / ((*previous_report_blocks_)[report_block.sender_ssrc].first.second 
						* sqrt(2 * num_packets_per_ack * loss_event_rate / 3));				// Calculate the TCP rate with the same conditions
					RTC_LOG(LS_INFO) << "Calculated TCP rate: " << tcp_rate;
					uint16_t sending_rate = rtp_sender_->ActualSendBitrateKbit() * 1000;	// Calculate the actual UDP sending rate
					RTC_LOG(LS_INFO) << "Calculated UDP rate: " << sending_rate;
					if (sending_rate > 10 * tcp_rate && tcp_rate != 0) {					// If the actual rate is 10 times larger
						HandleTriggeredCircuitBreaker(kCongestion);							// The Congestion CB has triggered
					}
				}					
				RecalculateCbInterval(report_block.sender_ssrc);							// Calculate the new CB_INT for this SSRC
			}
		}
	}

	void CircuitBreaker::RecalculateCbInterval(uint32_t sender_ssrc) {
		media_framing_interval_ = GetInterFrameArrival();
		(*previous_report_blocks_)[sender_ssrc].first.first = static_cast<uint32_t>(
			  ceil(3 * fmin(fmax(fmax(10 * frame_group_size_ * media_framing_interval_, 10 * (*previous_report_blocks_)[sender_ssrc].first.second),
				                 3 * rtcp_interval_sender_estimate_), fmax(15, 3 * rtcp_interval_)) / (3 * rtcp_interval_sender_estimate_)));
		RTC_LOG(LS_INFO) << "CB_INTERVAL for SSRC: " << sender_ssrc << " updated to: " << (*previous_report_blocks_)[sender_ssrc].first.first;
	}

	uint32_t CircuitBreaker::CalculateMediaTimeout(uint32_t sender_ssrc) {
		uint32_t rtt = (*previous_report_blocks_)[sender_ssrc].first.second;
		return (ceil(5 * fmax(fmax(media_framing_interval_, rtt), rtcp_interval_sender_estimate_) / rtcp_interval_sender_estimate_));
	}

    // Calculate average packet size over the last 4*G packets
	void CircuitBreaker::PushPacketSize(size_t packet_size) {
		uint32_t size = static_cast<uint32_t>(packet_size);
		if (packet_sizes_.size() == 4 * frame_group_size_) {
			packet_sizes_.erase(packet_sizes_.begin());
		}
		packet_sizes_.push_back(size);
		uint32_t avg_packet_size = 0;
		for (std::vector<uint32_t>::iterator it = packet_sizes_.begin(); it != packet_sizes_.end(); it++) {
			avg_packet_size += (*it);
		}
		packet_size_ = avg_packet_size / packet_sizes_.size();
		RTC_LOG(LS_INFO) << "Average packet size updated to: " << packet_size_ << " after receiving packet of size: " << packet_size;
	}

	double CircuitBreaker::GetInterFrameArrival() {
		int new_frame_rate = rtp_sender_->GetSendFrameRate();
        RTC_LOG(LS_INFO) << "Media Framing Interval updated to: " << 1000.0 / new_frame_rate << " from Send Frame Rate value of: " << new_frame_rate;
		// Interval has to be in ms and the frame rate is in frames per sec so 1s / fps * 1000
		return static_cast<double>(1000.0 / new_frame_rate);
	}

	// Calculated in the RTCP Sender class as part of official WebRTC library code and delivered to the Circuit Breaker class through the RTP RTCP Impl & the RTP Sender objects
	void CircuitBreaker::SetRtcpInterval(uint32_t rtcp_interval, uint32_t rtcp_interval_at_receiver_estimate) {
		rtcp_interval_sender_estimate_ = rtcp_interval_at_receiver_estimate;
		RTC_LOG(LS_INFO) << "RTCP Interval value in ms updated to: " << rtcp_interval;
		rtcp_interval_ = rtcp_interval;
	}

	void CircuitBreaker::SetVideoCodecType(RtpVideoCodecTypes video_type) {
		switch (video_type) {
			// For VPX codecs the frame group size is 1 as they can adapt the frame rate on each frame.
			// In this case, the media framing interval equals the inter frame arrival.
		case(kRtpVideoVp8):
			frame_group_size_ = 1;
			media_framing_interval_ = GetInterFrameArrival();
			codec_ = "VP8";
			break;
		case(kRtpVideoVp9):
			frame_group_size_ = 1;
			media_framing_interval_ = GetInterFrameArrival();
			codec_ = "VP9";
			break;
			// The circuit breaker implementation for the H264 codec relies on some guesses on how it is handled by the library.
		case(kRtpVideoH264):
            frame_group_size_ = 1;
			media_framing_interval_ = GetInterFrameArrival();
            codec_ = "H264";
			break;
			// For generic video use VPX settings as other cases are too complicated to implement. 
			// In either case results would probably be inconsistent and should be discarded.
		case(kRtpVideoGeneric):
			frame_group_size_ = 1;
			media_framing_interval_ = GetInterFrameArrival();
			codec_ = "generic";
			break;
			// Information does not exist on when the video type might be None or Video/Stereo. 
			// These cases are ignored and should not be hit in a typical use case.
		case(kRtpVideoNone):
			codec_ = "novideo";
			break;
		case(kRtpVideoStereo):
			codec_ = "video/stereo";
			break;
		default:
			codec_ = "unknown";
			break;
		}
		RTC_LOG(LS_INFO) << "Video codec: " << codec_;
	}

	void CircuitBreaker::UpdateSendingStatus(bool sending) {
		sending_ = sending;
	}

	void CircuitBreaker::HandleTriggeredCircuitBreaker(CbTriggerTypes flag) {
		switch (flag) {
		case(0): break;
		case(1): rtcp_timeout_cb_ = true;
			//HandleRtcpTimeoutCb();
			break;
		case(2): media_timeout_cb_ = true;
			RTC_LOG(LS_INFO) << "!!! Media Timeout CB triggered !!! ";
			//HandleMediaTimeoutCb();
			break;
		case(3): congestion_cb_ = true;
			RTC_LOG(LS_INFO) << "!!! Congestion CB triggered !!! ";
			//HandleCongestionCb();
			break;
		case(4): media_usability_cb_ = true;
			//HandleMediaUsabilityCb();
		}
	}

	std::array<bool, 4> CircuitBreaker::GetCbStatus() {
		std::array<bool, 4> cb_status;
		cb_status[0] = media_timeout_cb_;
		cb_status[1] = rtcp_timeout_cb_;
		cb_status[2] = congestion_cb_;
		cb_status[3] = media_usability_cb_;
		return cb_status;
	}

	void CircuitBreaker::ClearCb() {
		media_timeout_cb_ = false;
		rtcp_timeout_cb_ = false;
		congestion_cb_ = false;
		media_usability_cb_ = false;
	}
}  // namespace webrtc
