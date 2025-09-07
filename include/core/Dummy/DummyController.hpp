#pragma once
#include "Controller.hpp"


namespace libera::core::dummy {

class DummyController : public Controller {
public:
    DummyController();
    ~DummyController();

   

protected:
    virtual void run() override; // the worker loop

};

} // namespace libera::core::dummy
