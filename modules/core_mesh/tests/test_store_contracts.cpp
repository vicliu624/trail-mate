#include "contract/store_contracts.h"
#include "fake/fake_mesh_dependencies.h"

int main()
{
    mesh::tests::FakeLocalIdentityStore local_store;
    mesh::tests::contract::runLocalIdentityStoreContract(local_store);

    mesh::tests::FakePeerKeyStore peer_store;
    mesh::tests::contract::runPeerKeyStoreContract(peer_store);

    return 0;
}
