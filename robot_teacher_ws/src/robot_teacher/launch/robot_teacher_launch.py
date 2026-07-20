# Copyright 2026 Candidate
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

"""
Launch file for the robot_teacher package.

Запускает все три ноды робота-педагога:
  - perception_node  (камера + детекция лица/жестов)
  - hearing_node     (микрофон + STT)
  - dialog_node      (LLM + логика)
"""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    # ── Launch arguments ────────────────────────────────────
    camera_id_arg = DeclareLaunchArgument(
        'camera_id', default_value='0',
        description='Camera device index for perception_node'
    )
    debug_window_arg = DeclareLaunchArgument(
        'show_debug_window', default_value='false',
        description='Show OpenCV debug window in perception_node'
    )
    llm_provider_arg = DeclareLaunchArgument(
        'llm_provider', default_value='anthropic',
        description='LLM provider: anthropic | openai'
    )
    language_arg = DeclareLaunchArgument(
        'language', default_value='ru',
        description='STT language code for Whisper API'
    )

    camera_id = LaunchConfiguration('camera_id')
    show_debug = LaunchConfiguration('show_debug_window')
    llm_provider = LaunchConfiguration('llm_provider')
    language = LaunchConfiguration('language')

    # ── Nodes ──────────────────────────────────────────────
    perception_node = Node(
        package='robot_teacher',
        executable='perception_node',
        name='perception_node',
        output='screen',
        parameters=[{
            'camera_id':          camera_id,
            'show_debug_window':  show_debug,
        }]
    )

    hearing_node = Node(
        package='robot_teacher',
        executable='hearing_node',
        name='hearing_node',
        output='screen',
        parameters=[{
            'sample_rate':       16000,
            'silence_timeout_s': 6.0,
            'max_record_s':      30.0,
            'whisper_model':     'whisper-1',
            'language':          language,
        }]
    )

    dialog_node = Node(
        package='robot_teacher',
        executable='dialog_node',
        name='dialog_node',
        output='screen',
        parameters=[{
            'llm_provider':     llm_provider,
            'anthropic_model':  'claude-sonnet-4-20250514',
            'openai_model':     'gpt-4o-mini',
            'max_history':      20,
        }]
    )

    return LaunchDescription([
        camera_id_arg,
        debug_window_arg,
        llm_provider_arg,
        language_arg,
        perception_node,
        hearing_node,
        dialog_node,
    ])
