![](cover.jpeg)

# auto-weather-control

> Control your air conditioner using an ESP8266, DHT22, and a Servo ;) 

## Disclaimer
Please do not treat this as a serious project, but feel free to proceed if you want to bring it to production 😉. This is just me trying to learn new things.

## 😛 Demo 

https://github.com/theapache64/auto-weather-control/assets/9678279/f51eaa5f-3f6f-400e-ae06-2793ad8e74a1

- Source : https://twitter.com/theapache64/status/1776351284066754667


## 🦿 Prerequisites

- ESP8266
- DHT22
- Servo
- and obviously, your AC remote 😛 (you could also use an IR emitter I guess, but I'm super lazy, so am gonna use my AC remote)

## 🧠 How it works?

- The DHT sensor provides room temperature and humidity values.
- Using these values, the program calculates a score.
- When the score exceeds a threshold, the servo will move from `0` to `180`, pressing the power button on the remote.
- The thresholds are coming from a [Google sheet](https://docs.google.com/spreadsheets/d/1nO-hcNX2naH7hCS0WbRd7r907SvisjhD96I8R1Wx1Fs/edit?usp=sharing), so it's easy to calibrate.
- All this data is synced to the same [Google sheet](https://docs.google.com/spreadsheets/d/1nO-hcNX2naH7hCS0WbRd7r907SvisjhD96I8R1Wx1Fs/edit?usp=sharing), so you can draw cool charts as well.

## ✍️ Author

👤 **theapache64**

* Twitter: <a href="https://twitter.com/theapache64" target="_blank">@theapache64</a>
* Email: theapache64@gmail.com

Feel free to ping me 😉

## 🤝 Contributing

Contributions are what make the open source community such an amazing place to be learn, inspire, and create. Any
contributions you make are **greatly appreciated**.

1. Open an issue first to discuss what you would like to change.
1. Fork the Project
1. Create your feature branch (`git checkout -b feature/amazing-feature`)
1. Commit your changes (`git commit -m 'Add some amazing feature'`)
1. Push to the branch (`git push origin feature/amazing-feature`)
1. Open a pull request

Please make sure to update tests as appropriate.

## ❤ Show your support

Give a ⭐️ if this project helped you!

<a href="https://www.buymeacoffee.com/theapache64" target="_blank">
    <img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" width="160">
</a>


## 📝 License

```
Copyright © 2024 - theapache64

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

_This README was generated by [readgen](https://github.com/theapache64/readgen)_ ❤
