#pragma once
#include "libera/core/LaserDeviceBase.hpp"


namespace libera::core::dummy {

class DummyController : public LaserDeviceBase {
public:
    DummyController();
    ~DummyController();

   

protected:
    virtual void run() override; // the worker loop

};

} // namespace libera::core::dummy
