#ifndef MULTIVERSO_ZOO_H_
#define MULTIVERSO_ZOO_H_

#include <atomic>
#include <string>
#include <unordered_map>

#include "actor.h"
#include "node.h"
#include "multiverso/table_interface.h"

namespace multiverso {

class NetInterface;

// The dashboard 
// 1. Manage all components in the system, include all actors, and network env
// 2. Maintain system information, provide method to access this information
// 3. Control the system, to start and end
class Zoo {
public:
  ~Zoo();
  static Zoo* Get() { static Zoo zoo; return &zoo; };

  // Start all actors
  void Start(int role);
  // Stop all actors
  void Stop(bool finalize_net);

  void Barrier();

  void Deliver(const std::string& name, MessagePtr&);
  void Accept(MessagePtr& msg);

  int rank() const;
  int size() const;

  // TODO(to change)
  int num_workers() const { return num_workers_; }
  int num_servers() const { return num_servers_; }


  int RegisterTable(WorkerTable* worker_table);
  int RegisterTable(ServerTable* server_table);

private:
  // private constructor
  Zoo();
  void RegisterActor(const std::string name, Actor* actor) {
    CHECK(zoo_[name] == nullptr);
    zoo_[name] = actor;
  }

  void RegisterNode();

  friend class Actor;

  std::unordered_map<std::string, Actor*> zoo_;

  std::unique_ptr<MtQueue<MessagePtr>> mailbox_;
  std::atomic_int id_;

  NetInterface* net_util_;

  std::vector<Node> nodes_;
  int num_workers_;
  int num_servers_;

};

}

#endif // MULTIVERSO_ZOO_H_