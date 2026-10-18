#include "../../apps/pet/status_delivery.hpp"
#include "../support/test.hpp"

using namespace std::chrono_literals;
namespace ui = mirage::pet_ui;
namespace pet = mirage::runtime::pet;

int main() {
    const auto received = ui::StatusDelivery::Clock::time_point{} + 10s;
    pet::Status status;
    status.owner_epoch = "owner";
    status.source_id = "request-1";
    status.revision = 7;
    status.bubble.emplace();
    status.bubble->kind = "dialog";
    status.bubble->request_id = "request-1";
    status.bubble->text = "尚未验证";
    status.bubble->final = true;
    status.bubble->remaining_ms = 5000;
    const ui::StatusDelivery delivery{status, received};
    auto after = delivery.at(received + 3500ms);
    MIRAGE_CHECK(after.bubble && after.bubble->remaining_ms == 1500);
    MIRAGE_CHECK(after.owner_epoch == "owner" && after.source_id == "request-1" &&
                 after.revision == 7 && after.bubble->text == "尚未验证");
    MIRAGE_CHECK(delivery.at(received + 4999ms).bubble->remaining_ms == 1);
    MIRAGE_CHECK(!delivery.at(received + 5000ms).bubble);
    MIRAGE_CHECK(!delivery.at(received + 60s).bubble); // hidden longer than final lifetime
    MIRAGE_CHECK(delivery.at(received - 1s).bubble->remaining_ms == 5000);
    MIRAGE_CHECK(delivery.status.bubble->remaining_ms == 5000); // mailbox receipt is immutable
    MIRAGE_CHECK(delivery.at(received + 4s).bubble->remaining_ms == 1000);
    MIRAGE_CHECK(delivery.at(received + 4500ms).bubble->remaining_ms == 500);
    // A fresh request arriving while hidden replaces, rather than replays, the
    // expired final. Thinking has no final-expiry deadline.
    status.source_id = "request-2";
    status.bubble->request_id = "request-2";
    status.bubble->kind = "thinking";
    status.bubble->final = false;
    const ui::StatusDelivery thinking{status, received + 20s};
    after = thinking.at(received + 60s);
    MIRAGE_CHECK(after.source_id == "request-2" && after.bubble &&
                 after.bubble->request_id == "request-2");
    status.bubble->final = true;
    status.bubble->remaining_ms = 0;
    MIRAGE_CHECK(!(ui::StatusDelivery{status, received}.at(received)).bubble);
    status.bubble.reset();
    MIRAGE_CHECK(!(ui::StatusDelivery{status, received}.at(received + 60s)).bubble);
    return mirage::testing::finish("pet_status_delivery_test");
}
