// Created by Qing Yang on 3/11/20.
// Copyright © 2020 Airbnb Inc. All rights reserved.

import UIKit
import StaticLib
import DynamicLib

class ViewController: UIViewController {
  override func viewDidLoad() {
    super.viewDidLoad()
    self.view.backgroundColor = UIColor.green
    let _ = BarClass()
    callDynamicLibFunction()
  }
}

