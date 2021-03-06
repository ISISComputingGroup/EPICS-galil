//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// Licence as published by the Free Software Foundation; either
// version 2.1 of the Licence, or (at your option) any later version.
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public Licence for more details.
//
// You should have received a copy of the GNU Lesser General Public
// Licence along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Contact details:
// mark.clift@synchrotron.org.au
// 800 Blackburn Road, Clayton, Victoria 3168, Australia.
//
// Single thread to manage disconnect/connect for all registered GalilController(s)

class GalilConnector: public epicsThreadRunable {
public:
  GalilConnector(void);
  void registerController(GalilController *cntrl);
  virtual void run();
  epicsThread thread;
  ~GalilConnector();

private:
  bool shuttingDown_;
  void shutdownConnector();
  vector<GalilController *> pCntrlList_;
};

