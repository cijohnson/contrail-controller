/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_STATE_MACHINE_H__
#define __BGP_STATE_MACHINE_H__

#include <boost/statechart/state_machine.hpp>

#include "base/queue_task.h"
#include "base/timer.h"
#include "bgp/bgp_proto.h"
#include "io/tcp_session.h"

namespace sc = boost::statechart;

class BgpPeer;
class BgpSession;
class BgpPeerInfo;
class BgpMessage;
class StateMachine;

namespace fsm {
struct Idle;
struct EvBgpNotification;
}

typedef boost::function<bool(StateMachine *)> EvValidate;

//
// This class implements the state machine for a BgpPeer. Note that a single
// state machine is used for a BgpPeer, instead of using one per TCP session.
// As a consequence, the state machine keeps track of the active and passive
// sessions to/from the peer.  Connection collision is resolved using remote
// and local router ids as specified in the RFC.  When the state machine has
// determined which session (active or passive) will be used in the steady
// state, ownership of that session is transferred to the peer and the other
// session, if any, is closed.
//
// Events for the state machine can be posted from a few different contexts.
// Administrative events are posted from the bgp::Config task, TCP related
// events from the ASIO thread, Timer events from bgp::StateMachine task and
// BGP Message related events are posted from the io::Reader task.
//
// Timers run in the context of the bgp::StateMachine task instead of running
// directly from the ASIO thread.  This avoids race conditions wherein timers
// expire at the same time that the state machine task is attempting to cancel
// them.
//
// TCP session related events are posted directly from the ASIO thread.  Race
// conditions wherein the bgp::StateMachine task tries to delete a session at
// the same time that the ASIO thread is attempting to notify an event for the
// session are avoided by have the state machine post a pseudo event to delete
// the session. See comments for the DeleteSession method for more information.
//
// All events on the state machine are processed asynchronously by enqueueing
// them to a WorkQueue. The WorkQueue is serviced by a bgp::StateMachine task.
// The BgpPeer index is used as the instance id for the task. This allows the
// state machines for multiple BgpPeers to run concurrently.
//
// Since events are processed asynchronously, it is possible that an event is
// no longer relevant by the time we get around to processing it. For example,
// we may see a TCP session close event after we've already decided to delete
// the session. The optional validate method in the event is used to determine
// if an event is still valid/relevant before feeding it to the state machine.
// Note that the validate routine needs to be run right before we process the
// event i.e. it's not correct to call it when posting the event.  Hence the
// need for the EventContainer structure.
//
class StateMachine : public sc::state_machine<StateMachine, fsm::Idle> {
public:
    typedef boost::function<void(void)> EventCB;

    static const int kOpenTime = 15;                // seconds
    static const int kConnectInterval = 30;         // seconds
    static const int kHoldTime = 90;                // seconds
    static const int kOpenSentHoldTime = 240;       // seconds
    static const int kIdleHoldTime = 5000;          // milliseconds
    static const int kMaxIdleHoldTime = 100 * 1000; // milliseconds
    static const int kJitter = 10;                  // percentage

    static const int GetDefaultHoldTime();

    enum State {
        IDLE        = 0,
        ACTIVE      = 1,
        CONNECT     = 2,
        OPENSENT    = 3,
        OPENCONFIRM = 4,
        ESTABLISHED = 5
    };

    StateMachine(BgpPeer *peer);
    ~StateMachine();

    void Initialize();
    void Shutdown();
    void SetAdminState(bool down);

    template <class Ev, int code> void OnIdle(const Ev &event);
    template <class Ev, int code> void OnIdleError(const Ev &event);
    void OnIdleNotification(const fsm::EvBgpNotification &event);

    int GetConnectTime() const;
    virtual void StartConnectTimer(int seconds);
    void CancelConnectTimer();
    bool ConnectTimerRunning();

    virtual void StartOpenTimer(int seconds);
    void CancelOpenTimer();
    bool OpenTimerRunning();

    virtual void StartHoldTimer();
    void CancelHoldTimer();
    bool HoldTimerRunning();

    virtual void StartIdleHoldTimer();
    void CancelIdleHoldTimer();
    bool IdleHoldTimerRunning();

    void StartSession();
    void DeleteSession(BgpSession *session);
    void AssignSession(bool active);

    void OnSessionEvent(TcpSession *session, TcpSession::Event event);
    bool PassiveOpen(BgpSession *session);

    void OnMessage(BgpSession *session, BgpProto::BgpMessage *msg);
    void OnMessageError(BgpSession *session, const ParseErrorContext *context);

    void SendNotificationAndClose(BgpSession *session,
        int code, int subcode = 0, const std::string &data = std::string());
    bool ProcessNotificationEvent(BgpSession *session);
    void SetDataCollectionKey(BgpPeerInfo *peer_info) const;

    const std::string &StateName() const;
    const std::string &LastStateName() const;

    BgpPeer *peer() { return peer_; }
    BgpSession *active_session();
    void set_active_session(BgpSession *session);
    BgpSession *passive_session();
    void set_passive_session(BgpSession *session);

    void connect_attempts_inc() { attempts_++; }
    void connect_attempts_clear() { attempts_ = 0; }

    int hold_time() const { return hold_time_; }
    void reset_hold_time();
    void set_hold_time(int hold_time);
    int idle_hold_time() const { return idle_hold_time_; }
    void reset_idle_hold_time() { idle_hold_time_ = 0; }
    void set_idle_hold_time(int idle_hold_time) { 
        idle_hold_time_ = idle_hold_time; 
    }

    void set_state(State state);
    State get_state() { return state_; }
    const std::string last_state_change_at() const;
    void set_last_event(const std::string &event);
    const std::string &last_event() const { return last_event_; }

    void set_last_notification_in(int code, int subcode,
        const std::string &reason);
    void set_last_notification_out(int code, int subcode,
        const std::string &reason);
    const std::string last_notification_out_error() const;
    const std::string last_notification_in_error() const;
    void reset_last_info();

private:
    friend class StateMachineTest;

    struct EventContainer {
        boost::intrusive_ptr<const sc::event_base> event;
        EvValidate validate;
    };

    bool ConnectTimerExpired();
    void FireConnectTimer();
    bool OpenTimerExpired();
    void FireOpenTimer();
    bool HoldTimerExpired();
    void FireHoldTimer();
    bool IdleHoldTimerExpired();
    void FireIdleHoldTimer();

    void TimerErrorHanlder(std::string name, std::string error) { }
    void DeleteAllTimers();

    template <typename Ev> bool Enqueue(const Ev &event);
    bool DequeueEvent(EventContainer ec);

    WorkQueue<EventContainer> work_queue_;
    BgpPeer *peer_;
    BgpSession *active_session_;
    BgpSession *passive_session_;
    Timer *connect_timer_;
    Timer *open_timer_;
    Timer *hold_timer_;
    Timer *idle_hold_timer_;
    int hold_time_;
    int idle_hold_time_;
    int attempts_;
    bool deleted_;
    State state_;
    State last_state_;
    std::string last_event_;
    uint64_t last_event_at_;
    uint64_t last_state_change_at_;
    std::pair<int, int> last_notification_in_;
    std::string last_notification_in_error_;
    uint64_t last_notification_in_at_;
    std::pair<int, int> last_notification_out_;
    std::string last_notification_out_error_;
    uint64_t last_notification_out_at_;

    DISALLOW_COPY_AND_ASSIGN(StateMachine);
};

std::ostream &operator<<(std::ostream &out, const StateMachine::State &state);

#endif
